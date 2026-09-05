!ifndef RCMUSIC_INSTALLER_FILES_INCLUDED
!define RCMUSIC_INSTALLER_FILES_INCLUDED

!include LogicLib.nsh
!include FileFunc.nsh

Var RcPath
Var RcHash
Var RcStatus
Var RcMutex

!macro RCMUSIC_FILES_SAVE
    Push $0
    Push $1
    Push $2
    Push $3
    Push $4
    Push $5
    Push $6
    Push $7
    Push $8
    Push $9
!macroend

!macro RCMUSIC_FILES_RESTORE
    Pop $9
    Pop $8
    Pop $7
    Pop $6
    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Pop $1
    Pop $0
!macroend

!macro RCMUSIC_INSTALLER_FILE_FUNCTIONS PREFIX
Function ${PREFIX}RCMusic.ValidatePath
    !insertmacro RCMUSIC_FILES_SAVE
    StrCpy $RcStatus 1
    StrCmp $RcPath "" validate_done
    ; Resolve lexical components once; the caller constrains the install root
    ; and payload ID. A bounded Win32 result must never be silently truncated.
    System::Call 'kernel32::GetFullPathNameW(w "$RcPath", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i.r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
        Goto validate_done
    ${EndIf}
validate_next:
    System::Call 'kernel32::GetFileAttributesW(w r0) i.r1 ?e'
    Pop $2
    ${If} $1 == -1
        ${If} $2 != 2
        ${AndIf} $2 != 3
            Goto validate_done
        ${EndIf}
    ${Else}
        IntOp $2 $1 & 0x400
        ${If} $2 != 0
            Goto validate_done
        ${EndIf}
    ${EndIf}
    ; GetParent of a drive root returns its drive designator. Do not turn the
    ; next lookup into a drive-relative path or loop forever at the root.
    StrLen $1 $0
    ${If} $1 == 3
        StrCpy $2 $0 2 1
        ${If} $2 == ":\"
            Goto validate_safe
        ${EndIf}
    ${EndIf}
    ${GetParent} "$0" $1
    ${If} $1 == ""
    ${OrIf} $1 == $0
        Goto validate_safe
    ${EndIf}
    StrLen $2 $1
    ${If} $2 == 2
        StrCpy $2 $1 1 1
        ${If} $2 == ":"
            StrCpy $1 "$1\"
        ${EndIf}
    ${EndIf}
    StrCpy $0 $1
    Goto validate_next
validate_safe:
    StrCpy $RcStatus 0
validate_done:
    !insertmacro RCMUSIC_FILES_RESTORE
FunctionEnd

Function ${PREFIX}RCMusic.HashFile
    !insertmacro RCMUSIC_FILES_SAVE
    StrCpy $RcHash ""
    Call ${PREFIX}RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto hash_done
    ${EndIf}
    StrCpy $RcStatus 1
    StrCpy $0 -1
    StrCpy $1 0
    StrCpy $2 0
    StrCpy $3 0
    ; Exclusive GENERIC_READ | DELETE verifies ordinary sharing/deletion
    ; access. OPEN_REPARSE_POINT prevents following a substituted final link.
    ; Ancestors are preflight-checked, not pinned against hostile writers.
    System::Call 'kernel32::CreateFileW(w "$RcPath", i 0x80010000, i 0, p 0, i 3, i 0x00200080, p 0) p.r0 ?e'
    Pop $6
    ${If} $0 == -1
        ${If} $6 == 2
        ${OrIf} $6 == 3
            StrCpy $RcStatus 2
        ${EndIf}
        Goto hash_cleanup
    ${EndIf}
    System::Alloc 65536
    Pop $3
    ${If} $3 == 0
        Goto hash_cleanup
    ${EndIf}
    ; FileAttributeTagInfo: validate attributes on the handle actually hashed.
    System::Call 'kernel32::GetFileInformationByHandleEx(p r0, i 9, p r3, i 8) i.r6'
    ${If} $6 == 0
        Goto hash_cleanup
    ${EndIf}
    System::Call '*$3(i .r6)'
    ; READONLY also blocks DeleteFile even when opening with DELETE succeeded.
    IntOp $6 $6 & 0x411
    ${If} $6 != 0
        Goto hash_cleanup
    ${EndIf}
    System::Call 'bcrypt::BCryptOpenAlgorithmProvider(*p .r1, w "SHA256", p 0, i 0) i.r6'
    ${If} $6 != 0
        Goto hash_cleanup
    ${EndIf}
    ; NULL hash-object buffer requests CNG-managed storage (Windows 7+).
    System::Call 'bcrypt::BCryptCreateHash(p r1, *p .r2, p 0, i 0, p 0, i 0, i 0) i.r6'
    ${If} $6 != 0
        Goto hash_cleanup
    ${EndIf}
hash_read:
    System::Call 'kernel32::ReadFile(p r0, p r3, i 65536, *i .r4, p 0) i.r6'
    ${If} $6 == 0
        Goto hash_cleanup
    ${EndIf}
    ${If} $4 == 0
        Goto hash_finish
    ${EndIf}
    System::Call 'bcrypt::BCryptHashData(p r2, p r3, i r4, i 0) i.r6'
    ${If} $6 != 0
        Goto hash_cleanup
    ${EndIf}
    Goto hash_read
hash_finish:
    System::Call 'bcrypt::BCryptFinishHash(p r2, p r3, i 32, i 0) i.r6'
    ${If} $6 != 0
        Goto hash_cleanup
    ${EndIf}
    StrCpy $4 0
hash_hex:
    IntOp $5 $3 + $4
    System::Call '*$5(&i1 .r6)'
    IntOp $6 $6 & 255
    IntFmt $8 "%02x" $6
    StrCpy $RcHash "$RcHash$8"
    IntOp $4 $4 + 1
    ${If} $4 < 32
        Goto hash_hex
    ${EndIf}
    StrCpy $RcStatus 0
hash_cleanup:
    ${If} $2 != 0
        System::Call 'bcrypt::BCryptDestroyHash(p r2) i.r6'
        ${If} $6 != 0
            StrCpy $RcStatus 1
        ${EndIf}
    ${EndIf}
    ${If} $1 != 0
        System::Call 'bcrypt::BCryptCloseAlgorithmProvider(p r1, i 0) i.r6'
        ${If} $6 != 0
            StrCpy $RcStatus 1
        ${EndIf}
    ${EndIf}
    ${If} $3 != 0
        System::Free $3
    ${EndIf}
    ${If} $0 != -1
        System::Call 'kernel32::CloseHandle(p r0) i.r6'
        ${If} $6 == 0
            StrCpy $RcStatus 1
        ${EndIf}
    ${EndIf}
    ${If} $RcStatus != 0
        StrCpy $RcHash ""
    ${EndIf}
hash_done:
    !insertmacro RCMUSIC_FILES_RESTORE
FunctionEnd

Function ${PREFIX}RCMusic.AcquireMutex
    !insertmacro RCMUSIC_FILES_SAVE
    StrCpy $RcStatus 0
    ${If} $RcMutex != ""
    ${AndIf} $RcMutex != 0
        Goto acquire_done
    ${EndIf}
    StrCpy $RcMutex 0
    StrCpy $RcStatus 1
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\RCMusic.Installer") p.r0'
    ${If} $0 == 0
        Goto acquire_done
    ${EndIf}
    System::Call 'kernel32::WaitForSingleObject(p r0, i 0) i.r1'
    ${If} $1 == 0
    ${OrIf} $1 == 128
        ; An abandoned mutex is acquired, but caller must still validate every
        ; active/pending marker before resuming an interrupted installation.
        StrCpy $RcMutex $0
        StrCpy $RcStatus 0
    ${Else}
        System::Call 'kernel32::CloseHandle(p r0)'
    ${EndIf}
acquire_done:
    !insertmacro RCMUSIC_FILES_RESTORE
FunctionEnd

Function ${PREFIX}RCMusic.ReleaseMutex
    !insertmacro RCMUSIC_FILES_SAVE
    StrCpy $RcStatus 0
    ${If} $RcMutex != ""
    ${AndIf} $RcMutex != 0
        System::Call 'kernel32::ReleaseMutex(p $RcMutex) i.r0'
        ${If} $0 == 0
            StrCpy $RcStatus 1
        ${EndIf}
        System::Call 'kernel32::CloseHandle(p $RcMutex) i.r0'
        ${If} $0 == 0
            StrCpy $RcStatus 1
        ${EndIf}
    ${EndIf}
    StrCpy $RcMutex 0
    !insertmacro RCMUSIC_FILES_RESTORE
FunctionEnd
!macroend

!endif
