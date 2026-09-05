# -*- coding: utf-8 -*-
!ifndef RCMUSIC_ASSOCIATIONS_INCLUDED
!define RCMUSIC_ASSOCIATIONS_INCLUDED

!ifndef RCMUSIC_REGISTRY_BASE
!error "RcMusicAssociations.nsh requires RCMUSIC_REGISTRY_BASE"
!endif
!ifmacrondef RCMUSIC_FOR_EACH_FORMAT
!error "RcMusicAssociations.nsh requires RCMUSIC_FOR_EACH_FORMAT"
!endif
!if ${NSIS_CHAR_SIZE} != 2
!error "RcMusicAssociations.nsh requires Unicode NSIS"
!endif

!include LogicLib.nsh

!define RCMUSIC_ASSOC_APP "${RCMUSIC_REGISTRY_BASE}\RC Music"
!define RCMUSIC_ASSOC_CAPABILITIES "${RCMUSIC_ASSOC_APP}\Capabilities"
!define RCMUSIC_ASSOC_CLASSES "${RCMUSIC_REGISTRY_BASE}\Classes"
!define RCMUSIC_ASSOC_REGISTERED "${RCMUSIC_REGISTRY_BASE}\RegisteredApplications"
!define RCMUSIC_ASSOC_PRODUCT "RCMusic-v1"
!define RCMUSIC_ASSOC_DESCRIPTION "Reprodutor de áudio local"

; Private helpers use $0-$5; public functions preserve these and $R0/$R1/$R9.
; ReadRegStr also accepts REG_EXPAND_SZ. Query REG_SZ explicitly instead and
; reject oversized strings or embedded/trailing data that NSIS cannot compare.
; Outputs: $2 = value, $3 = Win32 status (2/3 means absent).
!macro RCMUSIC_ASSOC_READ KEY NAME
    StrCpy $0 "${KEY}"
    StrCpy $1 "${NAME}"
    StrCpy $2 ""
    StrCpy $3 1
    IntOp $4 ${NSIS_MAX_STRLEN} * 2
    ; RRF_NOEXPAND | RRF_SUBKEY_WOW6464KEY | RRF_RT_REG_SZ.
    System::Call 'advapi32::RegGetValueW(p 0x80000001, w r0, w r1, i 0x10010002, *i .r5, w .r2, *i r4 r4) i.r3'
    ${If} $3 == 0
        StrLen $5 $2
        IntOp $5 $5 + 1
        IntOp $5 $5 * 2
        ${If} $4 != $5
            StrCpy $3 13
        ${EndIf}
    ${EndIf}
!macroend

; Distinguish an absent namespace from an existing but unmarked empty key.
!macro RCMUSIC_ASSOC_KEY_STATUS KEY
    StrCpy $0 "${KEY}"
    System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w r0, i 0, i 0x101, *p .r2) i.r3'
    ${If} $3 == 0
        System::Call 'advapi32::RegCloseKey(p r2)'
    ${EndIf}
!macroend

!macro RCMUSIC_ASSOC_REQUIRE KEY NAME EXPECTED
    !insertmacro RCMUSIC_ASSOC_READ "${KEY}" "${NAME}"
    ${If} $3 != 0
    ${OrIf} $2 S!= "${EXPECTED}"
        Goto rcmusic_register_failed
    ${EndIf}
!macroend

!macro RCMUSIC_ASSOC_CHECK_OPTIONAL KEY NAME EXPECTED
    !insertmacro RCMUSIC_ASSOC_READ "${KEY}" "${NAME}"
    ${If} $3 == 0
        ${If} $2 S!= "${EXPECTED}"
            Goto rcmusic_register_failed
        ${EndIf}
    ${ElseIf} $3 != 2
    ${AndIf} $3 != 3
        Goto rcmusic_register_failed
    ${EndIf}
!macroend

; An interrupted upgrade may have written either the previous or requested
; executable. Both are safe only inside namespaces with our product marker.
!macro RCMUSIC_ASSOC_CHECK_UPGRADE KEY NAME PREVIOUS REQUESTED
    !insertmacro RCMUSIC_ASSOC_READ "${KEY}" "${NAME}"
    ${If} $3 == 0
        ${If} $2 S!= "${PREVIOUS}"
        ${AndIf} $2 S!= "${REQUESTED}"
            Goto rcmusic_register_failed
        ${EndIf}
    ${ElseIf} $3 != 2
    ${AndIf} $3 != 3
        Goto rcmusic_register_failed
    ${EndIf}
!macroend

!macro RCMUSIC_ASSOC_WRITE KEY NAME VALUE
    ClearErrors
    WriteRegStr HKCU "${KEY}" "${NAME}" "${VALUE}"
    IfErrors rcmusic_register_failed
!macroend

!macro RCMUSIC_ASSOC_DELETE_MATCHING KEY NAME EXPECTED
    !insertmacro RCMUSIC_ASSOC_READ "${KEY}" "${NAME}"
    ${If} $3 == 0
    ${AndIf} $2 S== "${EXPECTED}"
        ClearErrors
        DeleteRegValue HKCU "${KEY}" "${NAME}"
        ${If} ${Errors}
            StrCpy $R9 1
        ${EndIf}
    ${ElseIf} $3 == 5
        StrCpy $R9 1
    ${EndIf}
!macroend

; Nonempty keys belong to their remaining values/subkeys. Never recurse.
!macro RCMUSIC_ASSOC_PRUNE KEY
    StrCpy $0 "${KEY}"
    System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w r0, i 0, i 0x101, *p .r2) i.r3'
    ${If} $3 == 0
        System::Call 'advapi32::RegQueryInfoKeyW(p r2, p 0, p 0, p 0, *i .r4, p 0, p 0, *i .r5, p 0, p 0, p 0, p 0) i.r3'
        System::Call 'advapi32::RegCloseKey(p r2)'
        ${If} $3 != 0
            StrCpy $R9 1
        ${ElseIf} $4 == 0
        ${AndIf} $5 == 0
            ClearErrors
            DeleteRegKey /ifempty HKCU "${KEY}"
            ${If} ${Errors}
                StrCpy $R9 1
            ${EndIf}
        ${EndIf}
    ${ElseIf} $3 != 2
    ${AndIf} $3 != 3
        StrCpy $R9 1
    ${EndIf}
!macroend

!macro RCMUSIC_ASSOC_PREFLIGHT_FORMAT EXTENSION DESCRIPTION
    !insertmacro RCMUSIC_ASSOC_KEY_STATUS "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}"
    ${If} $3 == 0
        !insertmacro RCMUSIC_ASSOC_REQUIRE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
        !insertmacro RCMUSIC_ASSOC_CHECK_UPGRADE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationOwner" "$R1" "$R0"
    ${ElseIf} $3 != 2
    ${AndIf} $3 != 3
        Goto rcmusic_register_failed
    ${EndIf}
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "" "${DESCRIPTION}"
    !insertmacro RCMUSIC_ASSOC_CHECK_UPGRADE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\DefaultIcon" "" '$\"$R1$\",0' '$\"$R0$\",0'
    !insertmacro RCMUSIC_ASSOC_CHECK_UPGRADE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell\open\command" "" '$\"$R1$\" -- $\"%1$\"' '$\"$R0$\" -- $\"%1$\"'
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_CLASSES}\.${EXTENSION}\OpenWithProgids" "RCMusic.Audio.${EXTENSION}" ""
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_CAPABILITIES}\FileAssociations" ".${EXTENSION}" "RCMusic.Audio.${EXTENSION}"
!macroend

!macro RCMUSIC_ASSOC_WRITE_FORMAT EXTENSION DESCRIPTION
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationOwner" "$R0"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "" "${DESCRIPTION}"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\DefaultIcon" "" '$\"$R0$\",0'
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell\open\command" "" '$\"$R0$\" -- $\"%1$\"'
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CLASSES}\.${EXTENSION}\OpenWithProgids" "RCMusic.Audio.${EXTENSION}" ""
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CAPABILITIES}\FileAssociations" ".${EXTENSION}" "RCMusic.Audio.${EXTENSION}"
!macroend

!macro RCMUSIC_ASSOC_REMOVE_FORMAT EXTENSION DESCRIPTION
    !insertmacro RCMUSIC_ASSOC_READ "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationProduct"
    ${If} $3 == 0
    ${AndIf} $2 S== "${RCMUSIC_ASSOC_PRODUCT}"
        !insertmacro RCMUSIC_ASSOC_READ "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationOwner"
        ${If} $3 == 0
        ${AndIf} $2 S== "$R0"
            !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CAPABILITIES}\FileAssociations" ".${EXTENSION}" "RCMusic.Audio.${EXTENSION}"
            !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\.${EXTENSION}\OpenWithProgids" "RCMusic.Audio.${EXTENSION}" ""
            !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell\open\command" "" '$\"$R0$\" -- $\"%1$\"'
            !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\DefaultIcon" "" '$\"$R0$\",0'
            !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "" "${DESCRIPTION}"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell\open\command"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell\open"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\shell"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}\DefaultIcon"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\.${EXTENSION}\OpenWithProgids"
            !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\.${EXTENSION}"
            ; Keep ownership evidence until every content deletion has succeeded.
            ${If} $R9 == 0
                !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationOwner" "$R0"
            ${EndIf}
            ${If} $R9 == 0
                !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
            ${EndIf}
            ${If} $R9 == 0
                !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CLASSES}\RCMusic.Audio.${EXTENSION}"
            ${EndIf}
        ${ElseIf} $3 == 5
            StrCpy $R9 1
        ${EndIf}
    ${ElseIf} $3 == 5
        StrCpy $R9 1
    ${EndIf}
!macroend

!macro RCMUSIC_ASSOC_BEGIN
    Push $R9
    Push $0
    Push $1
    Push $2
    Push $3
    Push $4
    Push $5
    Push $R0
    Push $R1
    SetRegView 64
    StrCpy $R9 0
    StrCpy $R0 "$INSTDIR\RCMusic.exe"
!macroend

!macro RCMUSIC_ASSOC_END
    Pop $R1
    Pop $R0
    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Pop $1
    Pop $0
    Exch $R9
!macroend

Function RCMusic.RegisterAssociations
    !insertmacro RCMUSIC_ASSOC_BEGIN
    ; The quoted command must fit without NSIS silently truncating its suffix.
    StrLen $0 $INSTDIR
    IntOp $0 $0 + 22
    ${If} $0 >= ${NSIS_MAX_STRLEN}
        Goto rcmusic_register_failed
    ${EndIf}
    StrCpy $R1 $R0
    !insertmacro RCMUSIC_ASSOC_KEY_STATUS "${RCMUSIC_ASSOC_APP}"
    ${If} $3 == 0
        !insertmacro RCMUSIC_ASSOC_REQUIRE "${RCMUSIC_ASSOC_APP}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
        !insertmacro RCMUSIC_ASSOC_READ "${RCMUSIC_ASSOC_APP}" "AssociationOwner"
        ${If} $3 == 0
            ${If} $2 == ""
                Goto rcmusic_register_failed
            ${EndIf}
            StrCpy $R1 $2
        ${ElseIf} $3 != 2
        ${AndIf} $3 != 3
            Goto rcmusic_register_failed
        ${EndIf}
    ${ElseIf} $3 != 2
    ${AndIf} $3 != 3
        Goto rcmusic_register_failed
    ${EndIf}

    ; Finish every collision check before the first registry write.
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_REGISTERED}" "RC Music" "${RCMUSIC_ASSOC_CAPABILITIES}"
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationName" "RC Music"
    !insertmacro RCMUSIC_ASSOC_CHECK_OPTIONAL "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationDescription" "${RCMUSIC_ASSOC_DESCRIPTION}"
    !insertmacro RCMUSIC_ASSOC_CHECK_UPGRADE "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationIcon" '$\"$R1$\",0' '$\"$R0$\",0'
    !insertmacro RCMUSIC_FOR_EACH_FORMAT RCMUSIC_ASSOC_PREFLIGHT_FORMAT

    ; Record our namespace before any child writes so a failed first install is
    ; recognizable. An upgrade keeps its previous active owner until completion.
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_APP}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_APP}" "AssociationOwner" "$R1"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationName" "RC Music"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationDescription" "${RCMUSIC_ASSOC_DESCRIPTION}"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationIcon" '$\"$R0$\",0'
    !insertmacro RCMUSIC_FOR_EACH_FORMAT RCMUSIC_ASSOC_WRITE_FORMAT
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_REGISTERED}" "RC Music" "${RCMUSIC_ASSOC_CAPABILITIES}"
    !insertmacro RCMUSIC_ASSOC_WRITE "${RCMUSIC_ASSOC_APP}" "AssociationOwner" "$R0"
    Goto rcmusic_register_done

rcmusic_register_failed:
    StrCpy $R9 1
rcmusic_register_done:
    !insertmacro RCMUSIC_ASSOC_END
FunctionEnd

Function un.RCMusic.UnregisterAssociations
    !insertmacro RCMUSIC_ASSOC_BEGIN
    ; A stale uninstaller must not touch shared registration owned by an upgrade.
    !insertmacro RCMUSIC_ASSOC_READ "${RCMUSIC_ASSOC_APP}" "AssociationProduct"
    ${If} $3 == 5
        StrCpy $R9 1
    ${EndIf}
    ${If} $3 != 0
    ${OrIf} $2 S!= "${RCMUSIC_ASSOC_PRODUCT}"
        Goto rcmusic_unregister_done
    ${EndIf}
    !insertmacro RCMUSIC_ASSOC_READ "${RCMUSIC_ASSOC_APP}" "AssociationOwner"
    ${If} $3 == 5
        StrCpy $R9 1
    ${EndIf}
    ${If} $3 != 0
    ${OrIf} $2 S!= "$R0"
        Goto rcmusic_unregister_done
    ${EndIf}

    !insertmacro RCMUSIC_FOR_EACH_FORMAT RCMUSIC_ASSOC_REMOVE_FORMAT
    !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_REGISTERED}" "RC Music" "${RCMUSIC_ASSOC_CAPABILITIES}"
    !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationName" "RC Music"
    !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationDescription" "${RCMUSIC_ASSOC_DESCRIPTION}"
    !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_CAPABILITIES}" "ApplicationIcon" '$\"$R0$\",0'
    !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CAPABILITIES}\FileAssociations"
    !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_CAPABILITIES}"
    !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_REGISTERED}"
    ${If} $R9 == 0
        !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_APP}" "AssociationOwner" "$R0"
    ${EndIf}
    ${If} $R9 == 0
        !insertmacro RCMUSIC_ASSOC_DELETE_MATCHING "${RCMUSIC_ASSOC_APP}" "AssociationProduct" "${RCMUSIC_ASSOC_PRODUCT}"
    ${EndIf}
    ${If} $R9 == 0
        !insertmacro RCMUSIC_ASSOC_PRUNE "${RCMUSIC_ASSOC_APP}"
    ${EndIf}

rcmusic_unregister_done:
    !insertmacro RCMUSIC_ASSOC_END
FunctionEnd

!endif
