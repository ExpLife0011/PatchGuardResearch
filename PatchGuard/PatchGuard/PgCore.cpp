#include "pch.h"

ULONG PgCoreGetCodeSize(PVOID VirtualAddress, LDE_DISASM Lde)
{
    if (Lde == NULL || VirtualAddress == NULL || !MmIsAddressValid(VirtualAddress))
        return 0;

    PUCHAR p = (PUCHAR)VirtualAddress;

    ULONG len = 0;
    ULONG size = 0;

    __try {
        while (true)
        {
            len = Lde(p, 64);
            size = size + len;

            if (len == 1 && *p == 0xc3)
            {
                break;
            }

            p = p + len;

            if (size > 0x2000)
                return 0;
        }
    }
    __except (1)
    {
        return 0;
    }

    return size;
}

NTSTATUS PgCoreinitialization(PPG_CORE_INFO pgCoreInfo)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    PVOID pMappedNtos = NULL;

    DWORD64 Key = 0x4808588948c48b48; // pg entry point code
    /*
    INIT:00000001408A9CFF 48 31 84 CA C0 00 00 00                       xor     [rdx+rcx*8+0C0h], rax
    INIT:00000001408A9D07 48 D3 C8                                      ror     rax, cl
    INIT:00000001408A9D0A 48 0F BB C0                                   btc     rax, rax
    */
    DWORD64 CmpAppendDllSectionKey = 0x000000c0ca843148;
    
    do
    {
        if (pgCoreInfo == NULL)
            break;

        RtlZeroMemory(pgCoreInfo, sizeof(PG_CORE_INFO));

        pgCoreInfo->LdeAsm = (LDE_DISASM)PgHelperGetLDE();

        if (pgCoreInfo->LdeAsm == NULL)
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        status = PgHelperMapFile(L"\\SystemRoot\\System32\\ntoskrnl.exe", &pMappedNtos);

        if (!NT_SUCCESS(status))
        {
            LOGF_ERROR("Map ntos faild : %p\r\n", status);
            break;
        }

        auto pInitDBGSection = PgHelperGetSection("INITKDBG", pMappedNtos);

        if (pInitDBGSection == NULL)
        {
            LOGF_ERROR("Get INITKDBG section faild.\r\n");
            status = STATUS_NOT_FOUND;
            break;
        }

        pgCoreInfo->NtosInitSizeOfRawData = pInitDBGSection->SizeOfRawData;

        PCHAR ptr = NULL;

        status = PgHelperScanSection("INITKDBG", pMappedNtos, (PUCHAR)&Key, 8, 0xcc, 0, (PVOID*)&ptr);

        if (!NT_SUCCESS(status))
        {
            DPRINT("Scan pgEntry code faild %p\r\n", status);
            break;
        }

        RtlCopyMemory(pgCoreInfo->PgEntryPointFiled, ptr, sizeof(pgCoreInfo->PgEntryPointFiled));

        for (size_t i = 0; i < sizeof(pgCoreInfo->PgEntryPointFiled) / 8; i++)
        {
            DPRINT("PgEntryPointFiled[%d]   %p\r\n", i, pgCoreInfo->PgEntryPointFiled[i]);
        }

        status = PgHelperScanSection("INIT", pMappedNtos, (PUCHAR)&CmpAppendDllSectionKey, 8, 0xcc, 0, (PVOID*)&ptr);

        if (!NT_SUCCESS(status))
        {
            DPRINT("Scan CmpAppendDllSection faild %p\r\n", status);
            break;
        }

        /*
        //            INIT:00000001408A9D10 8B 82 88 06 00 00                             mov     eax, [rdx+688h]
        //            */

        size_t scaned = 0;

        while (true)
        {
            auto len = pgCoreInfo->LdeAsm(ptr, 64);

            scaned = scaned + len;

            if (len == 6)
            {
                pgCoreInfo->PgEntryPointRVA = *(ULONG*)(ptr + 2);
                LOGF_DEBUG("PgEntryPointRVA:%p\r\n", pgCoreInfo->PgEntryPointRVA);

                status = STATUS_SUCCESS;

                break;
            }

            if (scaned > 0x100)
            {
                status = STATUS_NOT_FOUND;
                break;
            }

            ptr = ptr + len;
        }

        if (!NT_SUCCESS(status))
        {
            DPRINT("Get PgEntry RVA faild.\r\n");
            break;
        }

        /*
        RtlMinimalBarrier pg最后一个函数的这个 并且貌似最后的ret不加密
        1803 指向pg最后未加密的数据的偏移 0x6A8

        TKDBG:0000000140369F1D F3 90                                         pause
        INITKDBG:0000000140369F1F
        INITKDBG:0000000140369F1F                               loc_140369F1F:                          ; CODE XREF: RtlMinimalBarrier+1D↑j
        INITKDBG:0000000140369F1F 8B 01                                         mov     eax, [rcx]
        INITKDBG:0000000140369F21 41 23 C1                                      and     eax, r9d
        INITKDBG:0000000140369F24 41 3B C0                                      cmp     eax, r8d
        INITKDBG:0000000140369F27 75 F4                                         jnz     short loc_140369F1D
        INITKDBG:0000000140369F29 0F BA E2 10                                   bt      edx, 10h
        INITKDBG:0000000140369F2D 73 09                                         jnb     short loc_140369F38
        INITKDBG:0000000140369F2F B8 01 00 00 00                                mov     eax, 1
        INITKDBG:0000000140369F34 F0 01 41 04                                   lock add [rcx+4], eax
        INITKDBG:0000000140369F38
        INITKDBG:0000000140369F38                               loc_140369F38:                          ; CODE XREF: RtlMinimalBarrier+45↑j
        INITKDBG:0000000140369F38 32 C0                                         xor     al, al
        INITKDBG:0000000140369F3A C3                                            retn
        INITKDBG:0000000140369F3A                               RtlMinimalBarrier endp

        */

        pgCoreInfo->PgRtlMinimalBarrierFiled[0] = 0x0001b8097310e2ba;
        pgCoreInfo->PgRtlMinimalBarrierFiled[1] = 0xc032044101f00000;
        // retn

        /*
            INIT:00000001409ABA8F                               loc_1409ABA8F:                          ; CODE XREF: CmpAppendDllSection+8E↓j
            INIT:00000001409ABA8F 48 31 84 CA C0 00 00 00                       xor     [rdx+rcx*8+0C0h], rax
            INIT:00000001409ABA97 48 D3 C8                                      ror     rax, cl
            INIT:00000001409ABA9A 48 0F BB C0                                   btc     rax, rax
            INIT:00000001409ABA9E E2 EF                                         loop    loc_1409ABA8F

            0: kd> ?a3a00b5ab0c9b857-A3A03F5891C8B4E8
            Evaluate expression: -57165494549649 = ffffcc02`1f01036f
            0: kd> dps ffffcc02`1f01036f L120
            ffffcc02`1f01036f  903d3204`e5cc5ce4
            .................
            .................
            offset:0xc0
            ffffcc02`1f010437  00000000`00000000                                                        rcx = 1
            ffffcc02`1f01043f  00000000`00000000                                                        rcx = 2
            ffffcc02`1f010447  00000000`00000000                                                        rcx = 3
            ffffcc02`1f01044f  00000000`00000000                                                        rcx = 4
            ffffcc02`1f010457  fffff807`3162ce00 nt!ExAcquireResourceSharedLite                         rcx = 5
            ffffcc02`1f01045f  fffff807`3162ca20 nt!ExAcquireResourceExclusiveLite                      rcx = 6
            ffffcc02`1f010467  fffff807`3196a010 nt!ExAllocatePoolWithTag                               rcx = 7
            ffffcc02`1f01046f  fffff807`3196a0a0 nt!ExFreePool                                          rcx = 8
            ffffcc02`1f010477  fffff807`31c299e0 nt!ExMapHandleToPointer
            ffffcc02`1f01047f  fffff807`316b0060 nt!ExQueueWorkItem
                                                                                                        rcx = (offset-0xc0) / 8     offset != 0xc0
           */

        pgCoreInfo->PgContextFiled[0] = PgHelperGetRoutineName(L"ExAcquireResourceSharedLite");
        pgCoreInfo->PgContextFiled[1] = PgHelperGetRoutineName(L"ExAcquireResourceExclusiveLite");
        pgCoreInfo->PgContextFiled[2] = PgHelperGetRoutineName(L"ExAllocatePoolWithTag");
        pgCoreInfo->PgContextFiled[3] = PgHelperGetRoutineName(L"ExFreePool");

        auto p = PgHelperGetRoutineName(L"DbgBreakPointWithStatus");

        /*
        ffffbd07`c170e959  fffff802`d544d810 hal!HalReturnToFirmware+0xa0
        ffffbd07`c170e961  00000000`000000ae
        ffffbd07`c170e969  fffff802`d563f650 nt!KeBugCheckEx
        ffffbd07`c170e971  00000000`00000120
        ffffbd07`c170e979  fffff802`d56f7660 nt!KeBugCheck2
        ffffbd07`c170e981  00000000`00000de0
        ffffbd07`c170e989  fffff802`d56f87a0 nt!KiBugCheckDebugBreak
        ffffbd07`c170e991  00000000`000000b5
        ffffbd07`c170e999  fffff802`d564b080 nt!KiDebugTrapOrFault
        ffffbd07`c170e9a1  00000000`0000043f
        ffffbd07`c170e9a9  fffff802`d5648470 nt!DbgBreakPointWithStatus
        ffffbd07`c170e9b1  00000000`00000002
        ffffbd07`c170e9b9  fffff802`d5648610 nt!RtlCaptureContext
        ffffbd07`c170e9c1  00000000`00000137
        ffffbd07`c170e9c9  fffff802`d566144c nt!KeQueryCurrentStackInformation+0x1a906c
        ffffbd07`c170e9d1  00000000`00000074
        ffffbd07`c170e9d9  fffff802`d54b83e0 nt!KeQueryCurrentStackInformation
        ffffbd07`c170e9e1  00000000`0000018a
        ffffbd07`c170e9e9  fffff802`d563f9a0 nt!KiSaveProcessorControlState
        ffffbd07`c170e9f1  00000000`00000172
        ffffbd07`c170e9f9  fffff802`d56534c0 nt!memcpy
        ffffbd07`c170ea01  00000000`00000339
        ffffbd07`c170ea09  fffff802`d56e8710 nt!IoSaveBugCheckProgress
        ffffbd07`c170ea11  00000000`0000003d
        ffffbd07`c170ea19  fffff802`d553b240 nt!KeIsEmptyAffinityEx
        ffffbd07`c170ea21  00000000`00000029
        ffffbd07`c170ea29  fffff802`d5ccdb50 nt!VfNotifyVerifierOfEvent
        ffffbd07`c170ea31  00000000`00000120
        ffffbd07`c170ea39  fffff802`d5648e30 nt!guard_check_icall
        ffffbd07`c170ea41  00000000`0000004a
        ffffbd07`c170ea49  fffff802`d579e7d0 nt!KeGuardDispatchICall
        ffffbd07`c170ea51  00000000`00000006
        ffffbd07`c170ea59  fffff802`d58506b8 nt!HalPrivateDispatchTable+0x48
        ffffbd07`c170ea61  00000000`00000008
        */
        pgCoreInfo->PgDbgBreakPointWithStatusFiled[0] = p;
        pgCoreInfo->PgDbgBreakPointWithStatusFiled[1] = 2; // sizeof(DbgBreakPointWithStatus);

        if (p)
            LOGF_DEBUG("size:%p\r\n", PgCoreGetCodeSize((PVOID)p, pgCoreInfo->LdeAsm));

        p = PgHelperGetRoutineName(L"RtlCaptureContext");

        if (p)
            LOGF_DEBUG("size:%p\r\n", PgCoreGetCodeSize((PVOID)p, pgCoreInfo->LdeAsm));

        p = PgHelperGetRoutineName(L"memcpy");

        if (p)
            LOGF_DEBUG("size:%p\r\n", PgCoreGetCodeSize((PVOID)p, pgCoreInfo->LdeAsm));

        p = PgHelperGetRoutineName(L"KeIsEmptyAffinityEx");

        if (p)
            LOGF_DEBUG("size:%p\r\n", PgCoreGetCodeSize((PVOID)p, pgCoreInfo->LdeAsm));

        p = PgHelperGetRoutineName(L"KeBugCheckEx");

        if (p)
            LOGF_DEBUG("size:%p\r\n", PgCoreGetCodeSize((PVOID)p, pgCoreInfo->LdeAsm));

        status = STATUS_SUCCESS;

        for (size_t i = 0; i < sizeof(pgCoreInfo->PgContextFiled) / 8; i++)
        {
            if (pgCoreInfo->PgContextFiled[i] == 0)
            {
                status = STATUS_UNSUCCESSFUL;
                break;
            }
        }

    } while (false);

    if (pMappedNtos)
        ZwUnmapViewOfSection(ZwCurrentProcess(), pMappedNtos);

    if (!NT_SUCCESS(status))
    {
        if (pgCoreInfo->LdeAsm)
            ExFreePoolWithTag(pgCoreInfo->LdeAsm, 'edlk');

        pgCoreInfo->LdeAsm = NULL;
    }

    return status;
}

void PgCoreDumpPgContext(PVOID pgContext, SIZE_T size)
{
    PULONG64 p = (PULONG64)pgContext;

    for (size_t i = 0; i < size / 8 - 1; i++)
    {
        if (i < 0x19) // 0xc8 / 8 rcx = 1
            LOGF_INFO("%p   %p    rcx:%p    offset:%p\r\n", &p[i], p[i], 0, i * 8);
        else
            LOGF_INFO("%p   %p    rcx:%p    offset:%p\r\n", &p[i], p[i], i - 0x18, i * 8);
    }
}

// 只能解密0~911 比前面的更少
BOOLEAN PgCoreGetFirstRorKeyAndOffset(ULONG64* lpRorKey, ULONG64* lpOffset, PVOID pgContext, SIZE_T ContextSize, PPG_CORE_INFO pgCore)
{
    if (lpRorKey == NULL || lpOffset == NULL || pgContext == NULL || ContextSize == 0 || pgCore == NULL)
        return false;

    PULONG64 p = (PULONG64)pgContext;
    ULONG64 offset = 0;
    
    for (size_t i = 0; i < ContextSize / 8 - 1 - 1; i++)
    {
        if (p[i] == 0 && p[i + 1] == 0)
        {
            offset = (ULONG64)&p[i] - (ULONG64)pgContext;
            p = &p[i];
            break;
        }
    }

    if (offset <= 0xc0)
    {
        LOGF_ERROR("not find 0x8000000080\r\n");
        return false;
    }

    LOGF_INFO("GetKeyAndOffset -> offset:%p    p:%p\r\n", offset, p);

    ULONG64 rcx = (offset - 0xc0) / 8;
    ULONG64 rorKey = 0;

    while (offset > 0xc0)
    {
        offset = (ULONG64)&p[1] - (ULONG64)pgContext;

        rcx = (offset - 0xc0) / 8;

        rorKey = p[1] ^ 0;
        rorKey = __ror64(rorKey, rcx);
        rorKey = __btc64(rorKey, rorKey);

        if ((rorKey ^ p[0]) == 0)
        {
            LOGF_DEBUG("find first key:%p    offset:%p    p:%p    *p:%p\r\n", rorKey, offset, &p[1], p[1]);
            *lpRorKey = rorKey;
            *lpOffset = offset;
            return true;
        }

        p--;
        offset = offset - 8;

    }

    return false;
}

BOOLEAN PgCoreGetFirstRorKeyAndOffsetByC3(ULONG64* lpRorKey, ULONG64* lpOffset, PVOID pgContext, SIZE_T ContextSize, PPG_CORE_INFO pgCore)
{
    if (lpRorKey == NULL || lpOffset == NULL || pgContext == NULL || ContextSize == 0 || pgCore == NULL)
        return false;

    PULONG64 p = (PULONG64)pgContext;
    ULONG64 offset = 0;

    /*
    12:51:27.723	INF	FFFFD10B69E1A665   F73B466650BD8B99    rcx:000000000000343B    offset:000000000001A298
    12:51:27.723	INF	FFFFD10B69E1A66D   AFCD4B3315AF2611    rcx:000000000000343C    offset:000000000001A2A0
    12:51:27.723	INF	FFFFD10B69E1A675   B5CB9B264D27F895    rcx:000000000000343D    offset:000000000001A2A8
    12:51:27.723	INF	FFFFD10B69E1A67D   00000000000000C3    rcx:000000000000343E    offset:000000000001A2B0
    */
    for (size_t i = 0; i < ContextSize / 8 - 1 - 1; i++)
    {
        if (*p == 0x00000000000000C3)
        {
            p = p - 2;
            offset = (ULONG64)p - (ULONG64)pgContext;
            break;
        }

        p++;
    }

    if (offset <= 0xc0)
    {
        LOGF_ERROR("not find 0x00000000000000C3\r\n");
        return false;
    }

    LOGF_INFO("GetKeyAndOffset -> offset:%p    p:%p\r\n", offset, p);

    ULONG64 rorKey = p[1] ^ pgCore->PgRtlMinimalBarrierFiled[1];
    ULONG64 rcx = (offset - 0xc0) / 8 + 1;

    rorKey = __ror64(rorKey, rcx);
    rorKey = __btc64(rorKey, rorKey);

    if ((rorKey ^ p[0]) == pgCore->PgRtlMinimalBarrierFiled[0])
    {
        LOGF_INFO("get first rorkey success -> rcx:%p    offset:%p    rorkey:%p\r\n", rcx, offset + 8, p[1] ^ pgCore->PgRtlMinimalBarrierFiled[1]);
    }
    else
    {
        LOGF_ERROR("Get first check faild.\r\n");
        return false;
    }

    p = &p[1];
    rorKey = *p ^ pgCore->PgRtlMinimalBarrierFiled[1];
    offset = (ULONG64)p - (ULONG64)pgContext;

    while (offset > 0xc0)
    {
        rcx = (offset - 0xc0) / 8;

        auto Decryptd = *p ^ rorKey;

        LOGF_INFO("offset:%p    rcx:%p    encrypted:%p    decrypted:%p    rorkey:%p\r\n", offset, rcx, *p, Decryptd, rorKey);

        rorKey = __ror64(rorKey, rcx);
        rorKey = __btc64(rorKey, rorKey);

        p--;
        offset = offset - 8;
    }

    return false;
}

BOOLEAN PgCoreDecrytionPartDump(PULONG64 pgContext, SIZE_T ContextSize, PPG_CORE_INFO pCore)
{
    if (pCore->PgDbgBreakPointWithStatusFiled[0] == NULL)
        return false;

    BOOLEAN bFindDecryRorKey = false;
    ULONG64 rorkey = 0;
    size_t rcx = 0;

    size_t i = 0;
    ULONG64 lastRorkey = 0;
    ULONG64 offset = 0;

    for (i = 0; i < ContextSize / 8 - 1; i++)
    {
        offset = (ULONG64)&pgContext[i] - (ULONG64)pgContext;

        if (offset <= 0xc0)
            continue;

        rcx = (offset - 0xc0) / 8;

        rorkey = pgContext[i] ^ pCore->PgDbgBreakPointWithStatusFiled[1];
        lastRorkey = rorkey;

        rorkey = __ror64(rorkey, rcx);
        rorkey = __btc64(rorkey, rorkey);

        if ((rorkey ^ pgContext[i - 1]) == pCore->PgDbgBreakPointWithStatusFiled[0])
        {
            LOGF_INFO("hit key rcx:%p    offset:%p\r\n", rcx, offset);
            bFindDecryRorKey = true;
            break;
        }
    }



    if (bFindDecryRorKey == false)
    {
        LOGF_ERROR("not hit decrypt rorkey :<\r\n");
        return false;
    }

    PULONG64 p = (PULONG64)(offset + (ULONG64)pgContext);
    rorkey = lastRorkey;

    while (offset > 0xc0)
    {
        rcx = (offset - 0xc0) / 8;

        auto Decryptd = *p ^ rorkey;

        LOGF_INFO("offset:%p    rcx:%p    encrypted:%p    decrypted:%p    rorkey:%p\r\n", offset, rcx, *p, Decryptd, rorkey);

        rorkey = __ror64(rorkey, rcx);
        rorkey = __btc64(rorkey, rorkey);

        p--;
        offset = offset - 8;
    }

    return true;

}

BOOLEAN NTAPI PgCorePoolCallback(BOOLEAN bNonPagedPool, PVOID Va, SIZE_T size, UCHAR tag[4], PVOID context)
{
    if (bNonPagedPool == false)
        return true;

    if (size > 0x95000)
        return true;

    if (context == NULL)
        return false;

    PPG_CORE_INFO pCoreInfo = reinterpret_cast<PPG_CORE_INFO>(context);

    if (size < pCoreInfo->NtosInitSizeOfRawData)
        return true;
    
    PCHAR p = (PCHAR)Va;

    PCHAR pEnd = p + size - 0x8 * 4;

    PULONG64 CompareFields = NULL;

    /*
            INIT:00000001409ABA8F                               loc_1409ABA8F:                          ; CODE XREF: CmpAppendDllSection+8E↓j
            INIT:00000001409ABA8F 48 31 84 CA C0 00 00 00                       xor     [rdx+rcx*8+0C0h], rax
            INIT:00000001409ABA97 48 D3 C8                                      ror     rax, cl
            INIT:00000001409ABA9A 48 0F BB C0                                   btc     rax, rax
            INIT:00000001409ABA9E E2 EF                                         loop    loc_1409ABA8F

            0: kd> ?a3a00b5ab0c9b857-A3A03F5891C8B4E8
            Evaluate expression: -57165494549649 = ffffcc02`1f01036f
            0: kd> dps ffffcc02`1f01036f L120
            ffffcc02`1f01036f  903d3204`e5cc5ce4
            .................
            .................
            offset:0xc8
            ffffcc02`1f010437  00000000`00000000                                                        rcx = 1
            ffffcc02`1f01043f  00000000`00000000                                                        rcx = 2
            ffffcc02`1f010447  00000000`00000000                                                        rcx = 3
            ffffcc02`1f01044f  00000000`00000000                                                        rcx = 4
            ffffcc02`1f010457  fffff807`3162ce00 nt!ExAcquireResourceSharedLite                         rcx = 5
            ffffcc02`1f01045f  fffff807`3162ca20 nt!ExAcquireResourceExclusiveLite                      rcx = 6
            ffffcc02`1f010467  fffff807`3196a010 nt!ExAllocatePoolWithTag                               rcx = 7
            ffffcc02`1f01046f  fffff807`3196a0a0 nt!ExFreePool                                          rcx = 8
            ffffcc02`1f010477  fffff807`31c299e0 nt!ExMapHandleToPointer
            ffffcc02`1f01047f  fffff807`316b0060 nt!ExQueueWorkItem
                                                                                                        rcx = (offset-0xc0) / 8
                                                                                                        // offset = rcx * 8 + 0xc0;
           */
    do
    {
        CompareFields = (PULONG64)p;

        auto rorkey = pCoreInfo->PgContextFiled[3] ^ CompareFields[3];

        ULONG rcx = 8;

        rorkey = __ror64(rorkey, rcx);

        rorkey = __btc64(rorkey, rorkey);

        if ((rorkey ^ CompareFields[2]) == pCoreInfo->PgContextFiled[2])
        {
            LOGF_INFO("Tag: %.*s, Address: 0x%p, Size: 0x%p\r\n", 4, tag, Va, size);

            LOGF_INFO("hit pg -> rcx:%p    btckey:%p\r\n", rcx, rorkey);

            rcx--;

            rorkey = __ror64(rorkey, rcx);

            rorkey = __btc64(rorkey, rorkey);

            LOGF_INFO("next rcx:%p    btckey:%p    %p\r\n", rcx, rorkey, CompareFields[1] ^ pCoreInfo->PgContextFiled[1]);

            if (rorkey != (CompareFields[1] ^ pCoreInfo->PgContextFiled[1]))
                LOGF_ERROR("error rorkey -> not equal.\r\n");

            auto PgContext = CompareFields - 0x1d; // offset ExAcquireResourceSharedLite - 0xe8 = pg context    0x1d = e8 / 8
            auto PgContextSize = size - ((ULONG64)PgContext - (ULONG64)Va);

            LOGF_INFO("PgContext:%p    size:%p\r\n", PgContext, PgContextSize);

            //PgCoreDumpPgContext(PgContext, PgContextSize);
            ULONG64 offset = 0;
            PgCoreGetFirstRorKeyAndOffsetByC3(&rorkey, &offset, PgContext, PgContextSize, (PPG_CORE_INFO)context);
            PgCoreDecrytionPartDump(PgContext, PgContextSize, (PPG_CORE_INFO)context);

            return true;
        }

        p++;
    } while ((ULONG64)p < (ULONG64)pEnd);

    return true;
}

NTSTATUS PgCoreFindPgContext(PPG_CORE_INFO pgCoreInfo)
{
    LOGF_DEBUG("-----[PgCore] Find PgContext in IndependentPages.-----\r\n");
    auto status = PgIdpEnumIndependentPages(PgCorePoolCallback, pgCoreInfo);
    LOGF_DEBUG("-----[PgCore] Find PgContext in IndependentPages end.-----\r\n");

    if (!NT_SUCCESS(status))
        LOGF_ERROR("[PgCore] Enum IndependentPages return 0x%x\r\n", status);

    LOGF_DEBUG("-----[PgCore] Find PgContext in big pool.-----\r\n");
    status = PgHelperEnumBigPool(PgCorePoolCallback, pgCoreInfo, NULL);
    LOGF_DEBUG("-----[PgCore] Find PgContext in big pool end.-----\r\n");

    if (!NT_SUCCESS(status))
        LOGF_ERROR("[PgCore] Enum Big Pool return 0x%x\r\n", status);

    return status;
}