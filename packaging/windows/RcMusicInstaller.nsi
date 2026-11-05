Unicode true
SetCompressor zlib
!ifndef RCMUSIC_CONFIG
!error "RcMusicInstaller.nsi requires RCMUSIC_CONFIG"
!endif
!include "${RCMUSIC_CONFIG}"
!ifndef RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE
!define RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE 0
!endif
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} != 0
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} != 1
!error "Unknown download component availability"
!endif
!endif
!ifmacrondef RCMUSIC_ACTIVATION_FILE
!macro RCMUSIC_ACTIVATION_FILE CALLBACK
!macroend
!endif
!ifmacrondef RCMUSIC_ACTIVATION_EXTRACT
!macro RCMUSIC_ACTIVATION_EXTRACT
!macroend
!endif
!ifmacrondef RCMUSIC_VERIFY_ACTIVATION
!macro RCMUSIC_VERIFY_ACTIVATION PREFIX
!macroend
!endif
; Historical configs remain Lite. New plans always provide both fields.
!ifndef RCMUSIC_EDITION
!define RCMUSIC_EDITION "Lite"
!endif
!ifndef RCMUSIC_REQUIRES_WEBVIEW2
!define RCMUSIC_REQUIRES_WEBVIEW2 0
!endif
!if "${RCMUSIC_EDITION}" == "CompleteExperimental"
    !if ${RCMUSIC_REQUIRES_WEBVIEW2} != 1
        !error "CompleteExperimental requires the existing WebView2 prerequisite"
    !endif
    !define RCMUSIC_EDITION_WELCOME "Edição Completa experimental.$\r$\nA navegação requer WebView2 Evergreen x64 já instalado. Não baixa nem instala o runtime.$\r$\nTrocar de edição substitui a versão atual de RC Music, preservando dados e preferências.$\r$\n$\r$\n"
!else if "${RCMUSIC_EDITION}" == "Lite"
    !if ${RCMUSIC_REQUIRES_WEBVIEW2} != 0
        !error "Lite cannot require a browser runtime"
    !endif
    !define RCMUSIC_EDITION_WELCOME "Edição Lite, sem navegador interno.$\r$\nTrocar de edição substitui a versão atual de RC Music, preservando dados e preferências.$\r$\n$\r$\n"
!else
    !error "Unknown RC Music installer edition"
!endif
!ifndef RCMUSIC_SHORTCUT_ROOT
!error "RcMusicInstaller.nsi requires an explicit RCMUSIC_SHORTCUT_ROOT"
!endif
!include LogicLib.nsh
!include FileFunc.nsh
!include Sections.nsh
!include x64.nsh
!include "RcMusicInstallerFiles.nsh"
!if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 1
!include "RcMusicAssociationSections.nsh"
!endif
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
!define MUI_CUSTOMFUNCTION_GUIINIT RCMusic.InitializeDownloadComponents
!endif
!include MUI2.nsh

!define RCMUSIC_INSTALL_STATE "${RCMUSIC_REGISTRY_BASE}\RC Music Installer"
!define RCMUSIC_INSTALL_ARP "${RCMUSIC_REGISTRY_BASE}\Microsoft\Windows\CurrentVersion\Uninstall\RC Music"
!define RCMUSIC_INSTALL_PRODUCT "RCMusicInstaller-v1"
!define RCMUSIC_ASSOCIATION_CLEANUP_PROTOCOL "2"
; Existing production installers disabled associations and hashed UTF-8 [].
!define RCMUSIC_LEGACY_EMPTY_FORMAT_SIGNATURE "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945"
!define RCMUSIC_RESERVED_UNINSTALL "Uninstall.exe"
!define RCMUSIC_RESERVED_MARKER ".rcmusic-install.ini"

Name "RC Music"
!if "${RCMUSIC_EDITION}" == "CompleteExperimental"
Caption "RC Music — Completa experimental"
!endif
OutFile "${RCMUSIC_OUTPUT}"
RequestExecutionLevel user
ShowInstDetails show
ShowUninstDetails show
!if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 0
!define MUI_WELCOMEPAGE_TEXT "${RCMUSIC_EDITION_WELCOME}Uso experimental local, não destinado à distribuição.$\r$\n$\r$\nRequer Windows x64 e Microsoft Visual C++ Redistributable compatível.$\r$\n$\r$\nAs associações de arquivos estão desabilitadas neste artefato."
!else
!define MUI_WELCOMEPAGE_TEXT "${RCMUSIC_EDITION_WELCOME}Uso experimental local, não destinado à distribuição.$\r$\n$\r$\nRequer Windows x64 e Microsoft Visual C++ Redistributable compatível.$\r$\n$\r$\nO aplicativo não altera a escolha de player padrão do Windows."
!endif
!insertmacro MUI_PAGE_WELCOME
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
!insertmacro MUI_PAGE_COMPONENTS
!endif
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"

Var RcValue
Var RcActive
Var RcPending
Var RcPrevious
Var RcPreviousDirectory
Var RcPreviousHash
Var RcPreviousFormatSignature
Var RcChildMode
Var RcVerifyOnly
Var RcAllowMissing
Var RcUninstallerHash
Var RcOwnedState
Var RcFreshThisAttempt
Var RcShortcutHash
Var RcShortcutPendingHash
Var RcShortcutObserved
Var RcShortcutCandidateHash
Var RcShortcutTemporary
Var RcShortcutProblem
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
Var RcDownloadSelected
Var RcPayloadId
!endif

!insertmacro RCMUSIC_INSTALLER_FILE_FUNCTIONS ""
!insertmacro RCMUSIC_INSTALLER_FILE_FUNCTIONS "un."

; Registry reads are exact REG_SZ, never expanded or silently truncated.
!macro RCMUSIC_INSTALL_READ KEY NAME
    StrCpy $0 "${KEY}"
    StrCpy $1 "${NAME}"
    StrCpy $2 ""
    IntOp $4 ${NSIS_MAX_STRLEN} * 2
    System::Call 'advapi32::RegGetValueW(p 0x80000001, w r0, w r1, i 0x10010002, *i .r5, w .r2, *i r4 r4) i.r3'
    ${If} $3 == 0
        StrLen $5 $2
        IntOp $5 $5 + 1
        IntOp $5 $5 * 2
        ${If} $4 != $5
            StrCpy $3 13
        ${EndIf}
    ${EndIf}
    StrCpy $RcValue $2
    StrCpy $RcStatus $3
!macroend

!macro RCMUSIC_INSTALL_WRITE KEY NAME VALUE
    ClearErrors
    WriteRegStr HKCU "${KEY}" "${NAME}" "${VALUE}"
    IfErrors rcmusic_install_failed
!macroend

!macro RCMUSIC_INSTALL_OPTIONAL KEY NAME OLD NEW
    !insertmacro RCMUSIC_INSTALL_READ "${KEY}" "${NAME}"
    ${If} $RcStatus == 0
        ${If} $RcValue S!= "${OLD}"
        ${AndIf} $RcValue S!= "${NEW}"
            Goto rcmusic_install_failed
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        Goto rcmusic_install_failed
    ${EndIf}
!macroend

!macro RCMUSIC_UNINSTALL_MATCHING KEY NAME EXPECTED
    !insertmacro RCMUSIC_INSTALL_READ "${KEY}" "${NAME}"
    ${If} $RcStatus == 0
    ${AndIf} $RcValue S== "${EXPECTED}"
        ClearErrors
        DeleteRegValue HKCU "${KEY}" "${NAME}"
        IfErrors rcmusic_uninstall_failed
    ${ElseIf} $RcStatus == 5
        Goto rcmusic_uninstall_failed
    ${EndIf}
!macroend

!macro RCMUSIC_VERIFY_FILE PREFIX RELATIVE HASH
    StrCpy $RcPath "$INSTDIR\${${RELATIVE}}"
    Call ${PREFIX}RCMusic.HashFile
    ${If} $RcStatus == 2
    ${AndIf} $RcAllowMissing == 1
        ; An earlier explicit uninstall may already have removed this file.
    ${ElseIf} $RcStatus != 0
        Goto rcmusic_verify_failed
    ${ElseIf} $RcHash S!= "${HASH}"
        Goto rcmusic_verify_failed
    ${EndIf}
!macroend
!macro RCMUSIC_VERIFY_INSTALL_FILE RELATIVE HASH
    !insertmacro RCMUSIC_VERIFY_FILE "" "${RELATIVE}" "${HASH}"
!macroend
!macro RCMUSIC_VERIFY_UNINSTALL_FILE RELATIVE HASH
    !insertmacro RCMUSIC_VERIFY_FILE "un." "${RELATIVE}" "${HASH}"
!macroend

!macro RCMUSIC_VERIFY_FUNCTION PREFIX CALLBACK
Function ${PREFIX}RCMusic.VerifyPreparedPayload
    !insertmacro RCMUSIC_PAYLOAD_FILES ${CALLBACK}
    StrCpy $RcStatus 0
    Return
rcmusic_verify_failed:
    StrCpy $RcStatus 1
FunctionEnd

Function ${PREFIX}RCMusic.VerifyPayload
    Call ${PREFIX}RCMusic.VerifyPreparedPayload
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    !insertmacro RCMUSIC_VERIFY_ACTIVATION "${PREFIX}"
    StrCpy $RcStatus 0
    Return
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
rcmusic_verify_failed:
    StrCpy $RcStatus 1
!endif
FunctionEnd

Function ${PREFIX}RCMusic.VerifyMarker
    StrCpy $RcPath "$INSTDIR\.rcmusic-install.ini"
    Call ${PREFIX}RCMusic.HashFile
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "Product"
    ${If} $RcValue S!= "${RCMUSIC_INSTALL_PRODUCT}"
        Goto rcmusic_marker_failed
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "PayloadId"
    ${If} $RcValue S!= "${RCMUSIC_PAYLOAD_ID}"
        Goto rcmusic_marker_failed
    ${EndIf}
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
    ReadINIStr $RcValue "$RcPath" "RC Music" "DownloadComponent"
    ${If} $RcValue S!= $RcDownloadSelected
        Goto rcmusic_marker_failed
    ${EndIf}
!endif
    ReadINIStr $RcValue "$RcPath" "RC Music" "FormatSignature"
    ${If} $RcValue S!= "${RCMUSIC_FORMAT_SIGNATURE}"
        Goto rcmusic_marker_failed
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "AssociationCleanupProtocol"
    ${If} $RcValue S!= "${RCMUSIC_ASSOCIATION_CLEANUP_PROTOCOL}"
        Goto rcmusic_marker_failed
    ${EndIf}
    ReadINIStr $RcUninstallerHash "$RcPath" "RC Music" "UninstallerHash"
    StrCpy $RcPath "$INSTDIR\Uninstall.exe"
    Call ${PREFIX}RCMusic.HashFile
    ${If} $RcStatus != 0
    ${OrIf} $RcHash S!= $RcUninstallerHash
        Goto rcmusic_marker_failed
    ${EndIf}
    StrCpy $RcStatus 0
    Return
rcmusic_marker_failed:
    StrCpy $RcStatus 1
FunctionEnd

; Only an opaque lowercase content ID can be converted into a version path.
Function ${PREFIX}RCMusic.ValidateId
    StrCpy $RcStatus 1
    StrLen $0 $RcValue
    ${If} $0 != 64
        Return
    ${EndIf}
    StrCpy $0 0
rcmusic_id_loop:
    StrCpy $1 $RcValue 1 $0
    StrCmp $1 "" rcmusic_id_valid
    StrCpy $2 0
rcmusic_id_character:
    StrCpy $3 "0123456789abcdef" 1 $2
    StrCmp $3 "" rcmusic_id_done
    StrCmp $1 $3 rcmusic_id_next
    IntOp $2 $2 + 1
    Goto rcmusic_id_character
rcmusic_id_next:
    IntOp $0 $0 + 1
    Goto rcmusic_id_loop
rcmusic_id_valid:
    StrCpy $RcStatus 0
rcmusic_id_done:
FunctionEnd
!macroend
!insertmacro RCMUSIC_VERIFY_FUNCTION "" RCMUSIC_VERIFY_INSTALL_FILE
!insertmacro RCMUSIC_VERIFY_FUNCTION "un." RCMUSIC_VERIFY_UNINSTALL_FILE

!macro RCMUSIC_SHORTCUT_PREFLIGHT_FUNCTION PREFIX
Function ${PREFIX}RCMusic.PreflightShortcut
    StrCpy $RcShortcutProblem 0
    StrCpy $RcShortcutHash ""
    StrCpy $RcShortcutPendingHash ""
    StrCpy $RcShortcutObserved ""
    StrCpy $RcPath "${RCMUSIC_SHORTCUT_ROOT}"
    Call ${PREFIX}RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto rcmusic_shortcut_preflight_failed
    ${EndIf}
    System::Call 'kernel32::GetFileAttributesW(w "$RcPath") i.r0 ?e'
    Pop $1
    ${If} $0 == -1
        ${If} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_shortcut_preflight_failed
        ${EndIf}
    ${Else}
        IntOp $0 $0 & 0x10
        ${If} $0 == 0
            Goto rcmusic_shortcut_preflight_failed
        ${EndIf}
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "ShortcutHash"
    ${If} $RcStatus == 0
        StrCpy $RcShortcutHash $RcValue
        Call ${PREFIX}RCMusic.ValidateId
        ${If} $RcStatus != 0
            Goto rcmusic_shortcut_preflight_failed
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        Goto rcmusic_shortcut_preflight_failed
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "ShortcutPendingHash"
    ${If} $RcStatus == 0
        StrCpy $RcShortcutPendingHash $RcValue
        Call ${PREFIX}RCMusic.ValidateId
        ${If} $RcStatus != 0
            Goto rcmusic_shortcut_preflight_failed
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        Goto rcmusic_shortcut_preflight_failed
    ${EndIf}
    StrCpy $RcPath "${RCMUSIC_SHORTCUT_ROOT}\RC Music.lnk"
    Call ${PREFIX}RCMusic.HashFile
    ${If} $RcStatus == 2
        ; An absent link can be created, including during migration or repair.
        StrCpy $RcStatus 0
        Return
    ${EndIf}
    ${If} $RcStatus != 0
        Goto rcmusic_shortcut_preflight_failed
    ${EndIf}
    ${If} $RcHash S!= $RcShortcutHash
    ${AndIf} $RcHash S!= $RcShortcutPendingHash
        Goto rcmusic_shortcut_preflight_failed
    ${EndIf}
    StrCpy $RcShortcutObserved $RcHash
    StrCpy $RcStatus 0
    Return
rcmusic_shortcut_preflight_failed:
    StrCpy $RcShortcutProblem 1
    StrCpy $RcStatus 1
FunctionEnd
!macroend
!insertmacro RCMUSIC_SHORTCUT_PREFLIGHT_FUNCTION ""
!insertmacro RCMUSIC_SHORTCUT_PREFLIGHT_FUNCTION "un."

Function RCMusic.DiscardShortcutTemporary
    ${If} $RcShortcutTemporary != ""
        StrCpy $RcPath $RcShortcutTemporary
        Call RCMusic.ValidatePath
        ${If} $RcStatus == 0
            ClearErrors
            Delete "$RcShortcutTemporary"
            ${IfNot} ${Errors}
                StrCpy $RcShortcutTemporary ""
            ${EndIf}
        ${EndIf}
    ${EndIf}
FunctionEnd

Function RCMusic.PublishShortcut
    Call RCMusic.PreflightShortcut
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    ${If} $RcActive S== "${RCMUSIC_PAYLOAD_ID}"
    ${AndIf} $RcShortcutObserved != ""
    ${AndIf} $RcShortcutObserved S== $RcShortcutHash
    ${AndIf} $RcShortcutPendingHash == ""
        ; Preserve the exact bytes of an already published current link.
        Return
    ${EndIf}
    ClearErrors
    CreateDirectory "${RCMUSIC_SHORTCUT_ROOT}"
    IfErrors rcmusic_shortcut_publish_failed
    ; An adjacent unique temporary file keeps the final rename on one volume,
    ; including profiles whose Start Menu and TEMP reside on different drives.
    GetTempFileName $RcShortcutTemporary "${RCMUSIC_SHORTCUT_ROOT}"
    IfErrors rcmusic_shortcut_publish_failed
    SetOutPath "$INSTDIR"
    IfErrors rcmusic_shortcut_publish_failed
    CreateShortCut "$RcShortcutTemporary" "$INSTDIR\RCMusic.exe" "" "$INSTDIR\RCMusic.exe" 0 SW_SHOWNORMAL "" "RC Music"
    IfErrors rcmusic_shortcut_publish_failed
    StrCpy $RcPath $RcShortcutTemporary
    Call RCMusic.HashFile
    ${If} $RcStatus != 0
        Goto rcmusic_shortcut_publish_failed
    ${EndIf}
    StrCpy $RcShortcutCandidateHash $RcHash
    Call RCMusic.PreflightShortcut
    ${If} $RcStatus != 0
        Goto rcmusic_shortcut_publish_failed
    ${EndIf}
    ; A retry may observe the previous pending candidate already published.
    ; Promote it before replacing PendingHash, otherwise a failed second rename
    ; would lose the only ownership evidence for the currently visible link.
    ${If} $RcShortcutObserved != ""
    ${AndIf} $RcShortcutObserved S== $RcShortcutPendingHash
    ${AndIf} $RcShortcutObserved S!= $RcShortcutHash
        ClearErrors
        WriteRegStr HKCU "${RCMUSIC_INSTALL_STATE}" "ShortcutHash" "$RcShortcutObserved"
        IfErrors rcmusic_shortcut_publish_failed
    ${EndIf}
    ClearErrors
    WriteRegStr HKCU "${RCMUSIC_INSTALL_STATE}" "ShortcutPendingHash" "$RcShortcutCandidateHash"
    IfErrors rcmusic_shortcut_publish_failed
    StrCpy $RcPath "${RCMUSIC_SHORTCUT_ROOT}\RC Music.lnk"
    System::Call 'kernel32::MoveFileExW(w "$RcShortcutTemporary", w "$RcPath", i 0x9) i.r0 ?e'
    Pop $1
    ${If} $0 == 0
        Goto rcmusic_shortcut_publish_failed
    ${EndIf}
    StrCpy $RcShortcutTemporary ""
    StrCpy $RcPath "${RCMUSIC_SHORTCUT_ROOT}\RC Music.lnk"
    Call RCMusic.HashFile
    ${If} $RcStatus != 0
    ${OrIf} $RcHash S!= $RcShortcutCandidateHash
        Goto rcmusic_shortcut_publish_failed
    ${EndIf}
    ClearErrors
    WriteRegStr HKCU "${RCMUSIC_INSTALL_STATE}" "ShortcutHash" "$RcShortcutCandidateHash"
    IfErrors rcmusic_shortcut_publish_failed
    DeleteRegValue HKCU "${RCMUSIC_INSTALL_STATE}" "ShortcutPendingHash"
    IfErrors rcmusic_shortcut_publish_failed
    StrCpy $RcStatus 0
    Return
rcmusic_shortcut_publish_failed:
    Call RCMusic.DiscardShortcutTemporary
    StrCpy $RcShortcutProblem 1
    StrCpy $RcStatus 1
FunctionEnd

Function un.RCMusic.RemoveShortcut
    Call un.RCMusic.PreflightShortcut
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    ${If} $RcShortcutObserved != ""
        ClearErrors
        Delete "${RCMUSIC_SHORTCUT_ROOT}\RC Music.lnk"
        ${If} ${Errors}
            StrCpy $RcShortcutProblem 1
            StrCpy $RcStatus 1
            Return
        ${EndIf}
    ${EndIf}
    StrCpy $RcStatus 0
FunctionEnd

; The copied executable still uses its compile-time root/ID. /VERIFY is
; read-only and does not acquire the mutex already owned by this process.
Function RCMusic.RunPrevious
    StrCpy $RcValue $RcPrevious
    Call RCMusic.ValidateId
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    StrCpy $RcPreviousDirectory "${RCMUSIC_INSTALL_ROOT}\$RcPrevious"
    StrCpy $RcPath "$RcPreviousDirectory\.rcmusic-install.ini"
    Call RCMusic.HashFile
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "Product"
    ${If} $RcValue S!= "${RCMUSIC_INSTALL_PRODUCT}"
        Goto rcmusic_previous_failed
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "PayloadId"
    ${If} $RcValue S!= $RcPrevious
        Goto rcmusic_previous_failed
    ${EndIf}
    ReadINIStr $RcValue "$RcPath" "RC Music" "FormatSignature"
    Call RCMusic.ValidateId
    ${If} $RcStatus != 0
        Goto rcmusic_previous_failed
    ${EndIf}
    StrCpy $RcPreviousFormatSignature $RcValue
    ReadINIStr $RcValue "$RcPath" "RC Music" "AssociationCleanupProtocol"
    ${If} $RcValue S== ""
        ; Legacy cleanup cannot retire a subset after a new owner is published.
        ; Its own /VERIFY still authenticates the compiled signature below.
        ${If} $RcPreviousFormatSignature S!= "${RCMUSIC_FORMAT_SIGNATURE}"
        ${AndIf} $RcPreviousFormatSignature S!= "${RCMUSIC_LEGACY_EMPTY_FORMAT_SIGNATURE}"
            Goto rcmusic_previous_failed
        ${EndIf}
    ${ElseIf} $RcValue S!= "${RCMUSIC_ASSOCIATION_CLEANUP_PROTOCOL}"
        Goto rcmusic_previous_failed
    ${EndIf}
    ReadINIStr $RcPreviousHash "$RcPath" "RC Music" "UninstallerHash"
    StrCpy $RcPath "$RcPreviousDirectory\Uninstall.exe"
    Call RCMusic.HashFile
    ${If} $RcStatus != 0
    ${OrIf} $RcHash S!= $RcPreviousHash
        Goto rcmusic_previous_failed
    ${EndIf}
    InitPluginsDir
    Delete "$PLUGINSDIR\rcmusic-cleanup.exe"
    ClearErrors
    CopyFiles /SILENT "$RcPath" "$PLUGINSDIR\rcmusic-cleanup.exe"
    IfErrors rcmusic_previous_failed
    StrCpy $RcPath "$PLUGINSDIR\rcmusic-cleanup.exe"
    Call RCMusic.HashFile
    ${If} $RcStatus != 0
    ${OrIf} $RcHash S!= $RcPreviousHash
        Goto rcmusic_previous_failed
    ${EndIf}
    ClearErrors
    ExecWait '"$PLUGINSDIR\rcmusic-cleanup.exe" /S $RcChildMode _?=$RcPreviousDirectory' $0
    IfErrors rcmusic_previous_failed
    ${If} $0 != 0
        Goto rcmusic_previous_failed
    ${EndIf}
    StrCpy $RcStatus 0
    Return
rcmusic_previous_failed:
    StrCpy $RcStatus 1
FunctionEnd

; A failed fresh extraction has not published any Registry state. Only its
; compiled inventory can be removed; unknown entries and links are not followed.
!macro RCMUSIC_CLEANUP_FRESH_FILE RELATIVE HASH
    StrCpy $RcPath "$INSTDIR\${${RELATIVE}}"
    Call RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto rcmusic_fresh_cleanup_failed
    ${EndIf}
    System::Call 'kernel32::GetFileAttributesW(w "$RcPath") i.r0 ?e'
    Pop $1
    ${If} $0 == -1
        ${If} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_fresh_cleanup_failed
        ${EndIf}
    ${Else}
        IntOp $0 $0 & 0x10
        ${If} $0 != 0
            Goto rcmusic_fresh_cleanup_failed
        ${EndIf}
        ClearErrors
        Delete "$RcPath"
        IfErrors rcmusic_fresh_cleanup_failed
    ${EndIf}
!macroend
!macro RCMUSIC_CLEANUP_FRESH_DIRECTORY RELATIVE
    StrCpy $RcPath "$INSTDIR\${${RELATIVE}}"
    Call RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto rcmusic_fresh_cleanup_failed
    ${EndIf}
    System::Call 'kernel32::RemoveDirectoryW(w "$RcPath") i.r0 ?e'
    Pop $1
    ${If} $0 == 0
        ${If} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_fresh_cleanup_failed
        ${EndIf}
    ${EndIf}
!macroend
Function RCMusic.CleanupFresh
    ClearErrors
    SetOutPath "$TEMP"
    IfErrors rcmusic_fresh_cleanup_failed
    StrCpy $RcPath "$INSTDIR"
    Call RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Return
    ${EndIf}
    !insertmacro RCMUSIC_PAYLOAD_FILES RCMUSIC_CLEANUP_FRESH_FILE
    !insertmacro RCMUSIC_ACTIVATION_FILE RCMUSIC_CLEANUP_FRESH_FILE
    !insertmacro RCMUSIC_CLEANUP_FRESH_FILE RCMUSIC_RESERVED_UNINSTALL ""
    !insertmacro RCMUSIC_CLEANUP_FRESH_FILE RCMUSIC_RESERVED_MARKER ""
    !insertmacro RCMUSIC_PAYLOAD_DIRECTORIES RCMUSIC_CLEANUP_FRESH_DIRECTORY
    System::Call 'kernel32::RemoveDirectoryW(w "$INSTDIR") i.r0 ?e'
    Pop $1
    ${If} $0 == 0
        ${If} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_fresh_cleanup_failed
        ${EndIf}
    ${EndIf}
    StrCpy $RcStatus 0
    Return
rcmusic_fresh_cleanup_failed:
    StrCpy $RcStatus 1
FunctionEnd

Function .onInit
    ${IfNot} ${RunningX64}
        SetErrorLevel 1
        Abort "O RC Music requer Windows x64."
    ${EndIf}
    SetShellVarContext current
    SetRegView 64
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
    StrCpy $RcDownloadSelected 0
    StrCpy $RcPayloadId "${RCMUSIC_BASE_PAYLOAD_ID}"
!endif
    StrCpy $INSTDIR "${RCMUSIC_INSTALL_ROOT}\${RCMUSIC_PAYLOAD_ID}"
    Call RCMusic.AcquireMutex
    ${If} $RcStatus != 0
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Function un.onInit
    ${IfNot} ${RunningX64}
        SetErrorLevel 1
        Abort "Este desinstalador requer Windows x64."
    ${EndIf}
    SetShellVarContext current
    SetRegView 64
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
    ; NSIS supplies the original uninstall directory, including explicit _?=.
    ; Infer the inventory only from one of our two compiled version paths.
    ClearErrors
    GetFullPathName $0 "$INSTDIR"
    IfErrors rcmusic_uninstall_init_failed
    ${If} $0 == "${RCMUSIC_INSTALL_ROOT}\${RCMUSIC_BASE_PAYLOAD_ID}"
        StrCpy $RcDownloadSelected 0
        StrCpy $RcPayloadId "${RCMUSIC_BASE_PAYLOAD_ID}"
    ${ElseIf} $0 == "${RCMUSIC_INSTALL_ROOT}\${RCMUSIC_DOWNLOAD_PAYLOAD_ID}"
        StrCpy $RcDownloadSelected 1
        StrCpy $RcPayloadId "${RCMUSIC_DOWNLOAD_PAYLOAD_ID}"
    ${Else}
        Goto rcmusic_uninstall_init_failed
    ${EndIf}
    StrCpy $INSTDIR $0
!else
    StrCpy $INSTDIR "${RCMUSIC_INSTALL_ROOT}\${RCMUSIC_PAYLOAD_ID}"
!endif
    StrCpy $RcVerifyOnly 0
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/VERIFY" $1
    ${IfNot} ${Errors}
        StrCpy $RcVerifyOnly 1
    ${Else}
        Call un.RCMusic.AcquireMutex
        ${If} $RcStatus != 0
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${EndIf}
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
    Return
rcmusic_uninstall_init_failed:
    SetErrorLevel 1
    Abort
!endif
FunctionEnd

Function .onGUIEnd
    Call RCMusic.DiscardShortcutTemporary
    Call RCMusic.ReleaseMutex
FunctionEnd
Function un.onGUIEnd
    Call un.RCMusic.ReleaseMutex
FunctionEnd

Section "Instalar RC Music" SEC_RCMUSIC_BASE
    SectionIn RO
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
    Call RCMusic.SelectDownloadComponent
!endif
    StrCpy $RcFreshThisAttempt 0
    StrCpy $RcPath "$INSTDIR"
    Call RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto rcmusic_install_failed
    ${EndIf}
    StrCpy $RcActive ""
    StrCpy $RcPending ""
    StrCpy $RcPrevious ""
    StrCpy $RcOwnedState 0
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "Product"
    ${If} $RcStatus == 0
        ${If} $RcValue S!= "${RCMUSIC_INSTALL_PRODUCT}"
            Goto rcmusic_install_failed
        ${EndIf}
        StrCpy $RcOwnedState 1
        ; This records the last publication attempt, which can still be pending.
        ; Compatibility comes from the predecessor's marker and own /VERIFY.
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "FormatSignature"
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
        Call RCMusic.ValidateId
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "ActivePayload"
        ${If} $RcStatus == 0
            StrCpy $RcActive $RcValue
            Call RCMusic.ValidateId
            ${If} $RcStatus != 0
                Goto rcmusic_install_failed
            ${EndIf}
        ${ElseIf} $RcStatus != 2
        ${AndIf} $RcStatus != 3
            Goto rcmusic_install_failed
        ${EndIf}
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "PendingPayload"
        ${If} $RcStatus == 0
            ${If} $RcValue S!= "${RCMUSIC_PAYLOAD_ID}"
                Goto rcmusic_install_failed
            ${EndIf}
            StrCpy $RcPending $RcValue
        ${ElseIf} $RcStatus != 2
        ${AndIf} $RcStatus != 3
            Goto rcmusic_install_failed
        ${EndIf}
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "PendingCleanup"
        ${If} $RcStatus == 0
            StrCpy $RcPrevious $RcValue
            Call RCMusic.ValidateId
            ${If} $RcStatus != 0
                Goto rcmusic_install_failed
            ${EndIf}
            ${If} $RcActive S!= "${RCMUSIC_PAYLOAD_ID}"
            ${AndIf} $RcPending S!= "${RCMUSIC_PAYLOAD_ID}"
                Goto rcmusic_install_failed
            ${EndIf}
        ${ElseIf} $RcStatus != 2
        ${AndIf} $RcStatus != 3
            Goto rcmusic_install_failed
        ${EndIf}
    ${ElseIf} $RcStatus == 2
    ${OrIf} $RcStatus == 3
        System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w "${RCMUSIC_INSTALL_STATE}", i 0, i 0x101, *p .r0) i.r1'
        ${If} $1 == 0
            System::Call 'advapi32::RegCloseKey(p r0)'
            Goto rcmusic_install_failed
        ${ElseIf} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_install_failed
        ${EndIf}
    ${Else}
        Goto rcmusic_install_failed
    ${EndIf}

    ; A foreign ARP namespace is not adopted just because its name matches.
    ${If} $RcOwnedState == 0
        System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w "${RCMUSIC_INSTALL_ARP}", i 0, i 0x101, *p .r0) i.r1'
        ${If} $1 == 0
            System::Call 'advapi32::RegCloseKey(p r0)'
            Goto rcmusic_install_failed
        ${ElseIf} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_install_failed
        ${EndIf}
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_OPTIONAL "${RCMUSIC_INSTALL_ARP}" "DisplayName" "RC Music" "RC Music"
    !insertmacro RCMUSIC_INSTALL_OPTIONAL "${RCMUSIC_INSTALL_ARP}" "InstallLocation" "${RCMUSIC_INSTALL_ROOT}\$RcActive" "$INSTDIR"
    !insertmacro RCMUSIC_INSTALL_OPTIONAL "${RCMUSIC_INSTALL_ARP}" "UninstallString" '$\"${RCMUSIC_INSTALL_ROOT}\$RcActive\Uninstall.exe$\"' '$\"$INSTDIR\Uninstall.exe$\"'
    !insertmacro RCMUSIC_INSTALL_OPTIONAL "${RCMUSIC_INSTALL_ARP}" "DisplayVersion" "$RcActive" "${RCMUSIC_PAYLOAD_ID}"

    Call RCMusic.PreflightShortcut
    ${If} $RcStatus != 0
        Goto rcmusic_install_failed
    ${EndIf}

    ${If} $RcActive != ""
    ${AndIf} $RcActive S!= "${RCMUSIC_PAYLOAD_ID}"
        StrCpy $RcPrevious $RcActive
        StrCpy $RcChildMode "/VERIFY"
        Call RCMusic.RunPrevious
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
    ${EndIf}
    System::Call 'kernel32::GetFileAttributesW(w "$INSTDIR") i.r0 ?e'
    Pop $1
    ${If} $0 != -1
        ; Existing directories are accepted only with complete matching evidence.
        Call RCMusic.VerifyMarker
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
        StrCpy $RcAllowMissing 0
        Call RCMusic.VerifyPayload
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
    ${Else}
        ${If} $1 != 2
        ${AndIf} $1 != 3
            Goto rcmusic_install_failed
        ${EndIf}
        StrCpy $RcFreshThisAttempt 1
        SetOverwrite off
        ClearErrors
        !insertmacro RCMUSIC_PAYLOAD_EXTRACT
        IfErrors rcmusic_install_failed
        StrCpy $RcAllowMissing 0
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
        ${If} $RcDownloadSelected == 1
            Call RCMusic.VerifyPreparedPayload
            ${If} $RcStatus != 0
                Goto rcmusic_install_failed
            ${EndIf}
            !insertmacro RCMUSIC_ACTIVATION_EXTRACT
        ${EndIf}
!endif
        Call RCMusic.VerifyPayload
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
        ClearErrors
        WriteUninstaller "$INSTDIR\Uninstall.exe"
        IfErrors rcmusic_install_failed
        StrCpy $RcPath "$INSTDIR\Uninstall.exe"
        Call RCMusic.HashFile
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
        StrCpy $RcUninstallerHash $RcHash
        ClearErrors
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "Product" "${RCMUSIC_INSTALL_PRODUCT}"
        IfErrors rcmusic_install_failed
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "PayloadId" "${RCMUSIC_PAYLOAD_ID}"
        IfErrors rcmusic_install_failed
!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "DownloadComponent" "$RcDownloadSelected"
        IfErrors rcmusic_install_failed
!endif
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "FormatSignature" "${RCMUSIC_FORMAT_SIGNATURE}"
        IfErrors rcmusic_install_failed
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "AssociationCleanupProtocol" "${RCMUSIC_ASSOCIATION_CLEANUP_PROTOCOL}"
        IfErrors rcmusic_install_failed
        WriteINIStr "$INSTDIR\.rcmusic-install.ini" "RC Music" "UninstallerHash" "$RcUninstallerHash"
        IfErrors rcmusic_install_failed
        FlushINI "$INSTDIR\.rcmusic-install.ini"
        Call RCMusic.VerifyMarker
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
    ${EndIf}

    ; Pending state retains both payloads after a partial association failure.
    StrCpy $RcFreshThisAttempt 0
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_STATE}" "Product" "${RCMUSIC_INSTALL_PRODUCT}"
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_STATE}" "FormatSignature" "${RCMUSIC_FORMAT_SIGNATURE}"
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_STATE}" "PendingPayload" "${RCMUSIC_PAYLOAD_ID}"
    ${If} $RcPrevious != ""
        !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_STATE}" "PendingCleanup" "$RcPrevious"
    ${EndIf}
    !if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 1
        Call RCMusic.RegisterAssociations
        Pop $0
        ${If} $0 != 0
            Goto rcmusic_install_failed
        ${EndIf}
    !endif
    Call RCMusic.PublishShortcut
    ${If} $RcStatus != 0
        Goto rcmusic_install_failed
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_ARP}" "DisplayName" "RC Music"
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_ARP}" "InstallLocation" "$INSTDIR"
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_ARP}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_ARP}" "DisplayVersion" "${RCMUSIC_PAYLOAD_ID}"
    !insertmacro RCMUSIC_INSTALL_WRITE "${RCMUSIC_INSTALL_STATE}" "ActivePayload" "${RCMUSIC_PAYLOAD_ID}"
    ClearErrors
    DeleteRegValue HKCU "${RCMUSIC_INSTALL_STATE}" "PendingPayload"
    IfErrors rcmusic_install_failed
    !if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 1
    !if "${RCMUSIC_REGISTRY_BASE}" == "Software"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    !endif
    !endif

    ; No state writes are permitted after this handoff. The old uninstaller
    ; revalidates ownership under its own mutex and clears PendingCleanup.
    Call RCMusic.ReleaseMutex
    ${If} $RcPrevious != ""
        StrCpy $RcChildMode ""
        Call RCMusic.RunPrevious
        ${If} $RcStatus != 0
            Goto rcmusic_install_failed
        ${EndIf}
    ${EndIf}
    SetErrorLevel 0
    Goto rcmusic_install_done
rcmusic_install_failed:
    Call RCMusic.DiscardShortcutTemporary
    ${If} $RcFreshThisAttempt == 1
        Call RCMusic.CleanupFresh
        ${If} $RcStatus != 0
            Call RCMusic.ReleaseMutex
            SetErrorLevel 1
            Abort "A cópia ficou incompleta e não pôde ser removida com segurança. Verifique o destino antes de uma nova tentativa; as versões anteriores não foram removidas."
        ${EndIf}
    ${EndIf}
    Call RCMusic.ReleaseMutex
    SetErrorLevel 1
    ${If} $RcShortcutProblem == 1
        Abort "O atalho RC Music do menu Iniciar não pôde ser atualizado. Feche aplicações que o utilizem ou verifique se o atalho foi modificado antes de tentar novamente."
    ${EndIf}
    Abort "A instalação não foi concluída. Feche o RC Music e tente novamente; mantenha as versões existentes para recuperação."
rcmusic_install_done:
SectionEnd

!macro RCMUSIC_DELETE_FILE RELATIVE HASH
    ClearErrors
    Delete "$INSTDIR\${${RELATIVE}}"
    IfErrors rcmusic_uninstall_failed
!macroend
!macro RCMUSIC_DELETE_DIRECTORY RELATIVE
    StrCpy $RcPath "$INSTDIR\${${RELATIVE}}"
    System::Call 'kernel32::RemoveDirectoryW(w "$RcPath") i.r0 ?e'
    Pop $1
    ${If} $0 == 0
        ${If} $1 != 2
        ${AndIf} $1 != 3
        ${AndIf} $1 != 145
            Goto rcmusic_uninstall_failed
        ${EndIf}
    ${EndIf}
!macroend

!if ${RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE} == 1
Section /o "Ferramentas de download (opcional)" SEC_RCMUSIC_DOWNLOADS
    ; The mandatory section freezes this choice before any install mutation.
SectionEnd

Function RCMusic.InitializeDownloadComponents
    SectionSetSize ${SEC_RCMUSIC_BASE} ${RCMUSIC_BASE_SIZE_KB}
    SectionSetSize ${SEC_RCMUSIC_DOWNLOADS} ${RCMUSIC_DOWNLOAD_SIZE_KB}
FunctionEnd

Function RCMusic.SelectDownloadComponent
    StrCpy $RcDownloadSelected 0
    StrCpy $RcPayloadId "${RCMUSIC_BASE_PAYLOAD_ID}"
    SectionGetFlags ${SEC_RCMUSIC_DOWNLOADS} $0
    IntOp $0 $0 & ${SF_SELECTED}
    ${If} $0 != 0
        StrCpy $RcDownloadSelected 1
        StrCpy $RcPayloadId "${RCMUSIC_DOWNLOAD_PAYLOAD_ID}"
    ${EndIf}
    StrCpy $INSTDIR "${RCMUSIC_INSTALL_ROOT}\${RCMUSIC_PAYLOAD_ID}"
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_RCMUSIC_BASE} "Instala o player RC Music e preserva seus dados e preferências."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_RCMUSIC_DOWNLOADS} "Instala ferramentas locais de download e conversão para MP3. A extensão do Brave/Chromium exige ativação separada e não é instalada por esta opção."
!insertmacro MUI_FUNCTION_DESCRIPTION_END
!endif

Section "Uninstall"
    StrCpy $RcPath "$INSTDIR"
    Call un.RCMusic.ValidatePath
    ${If} $RcStatus != 0
        Goto rcmusic_uninstall_failed
    ${EndIf}
    System::Call 'kernel32::GetFileAttributesW(w "$INSTDIR") i.r0 ?e'
    Pop $1
    ${If} $0 == -1
        ${If} $RcVerifyOnly == 1
            Goto rcmusic_uninstall_failed
        ${EndIf}
        ${If} $1 == 2
        ${OrIf} $1 == 3
            Goto rcmusic_uninstall_done
        ${EndIf}
        Goto rcmusic_uninstall_failed
    ${EndIf}
    Call un.RCMusic.VerifyMarker
    ${If} $RcStatus != 0
        Goto rcmusic_uninstall_failed
    ${EndIf}
    StrCpy $RcAllowMissing 1
    ${If} $RcVerifyOnly == 1
        StrCpy $RcAllowMissing 0
    ${EndIf}
    Call un.RCMusic.VerifyPayload
    ${If} $RcStatus != 0
        Goto rcmusic_uninstall_failed
    ${EndIf}
    ${If} $RcVerifyOnly == 1
        Goto rcmusic_uninstall_done
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "Product"
    ${If} $RcStatus != 0
    ${OrIf} $RcValue S!= "${RCMUSIC_INSTALL_PRODUCT}"
        Goto rcmusic_uninstall_failed
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "PendingPayload"
    ${If} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        Goto rcmusic_uninstall_failed
    ${EndIf}
    StrCpy $RcActive ""
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "ActivePayload"
    ${If} $RcStatus == 0
        StrCpy $RcActive $RcValue
        Call un.RCMusic.ValidateId
        ${If} $RcStatus != 0
            Goto rcmusic_uninstall_failed
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        Goto rcmusic_uninstall_failed
    ${EndIf}
    ${If} $RcActive S== "${RCMUSIC_PAYLOAD_ID}"
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "PendingCleanup"
        ${If} $RcStatus == 0
            ${If} $RcValue S!= "${RCMUSIC_PAYLOAD_ID}"
                Call un.RCMusic.ReleaseMutex
                SetErrorLevel 1
                Abort "Há uma atualização com limpeza pendente. Conclua a atualização pelo instalador desta versão antes de desinstalar."
            ${EndIf}
        ${ElseIf} $RcStatus != 2
        ${AndIf} $RcStatus != 3
            Goto rcmusic_uninstall_failed
        ${EndIf}
        Call un.RCMusic.PreflightShortcut
        ${If} $RcStatus != 0
            Goto rcmusic_uninstall_failed
        ${EndIf}
    ${EndIf}
    !if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 1
        Call un.RCMusic.UnregisterAssociations
        Pop $0
        ${If} $0 != 0
            Goto rcmusic_uninstall_failed
        ${EndIf}
    !endif
    ${If} $RcActive S== "${RCMUSIC_PAYLOAD_ID}"
        Call un.RCMusic.RemoveShortcut
        ${If} $RcStatus != 0
            Goto rcmusic_uninstall_failed
        ${EndIf}
    ${EndIf}
    ; Every known file passed hash/reparse/lock checks before the first delete.
    ClearErrors
    SetOutPath "$TEMP"
    IfErrors rcmusic_uninstall_failed
    !insertmacro RCMUSIC_PAYLOAD_FILES RCMUSIC_DELETE_FILE
    !insertmacro RCMUSIC_ACTIVATION_FILE RCMUSIC_DELETE_FILE
    !insertmacro RCMUSIC_PAYLOAD_DIRECTORIES RCMUSIC_DELETE_DIRECTORY
    ${If} $RcActive S== "${RCMUSIC_PAYLOAD_ID}"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "ShortcutHash" "$RcShortcutHash"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "ShortcutPendingHash" "$RcShortcutPendingHash"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_ARP}" "DisplayName" "RC Music"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_ARP}" "InstallLocation" "$INSTDIR"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_ARP}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_ARP}" "DisplayVersion" "${RCMUSIC_PAYLOAD_ID}"
        DeleteRegKey /ifempty HKCU "${RCMUSIC_INSTALL_ARP}"
        !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "ActivePayload" "${RCMUSIC_PAYLOAD_ID}"
    ${EndIf}
    !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "PendingCleanup" "${RCMUSIC_PAYLOAD_ID}"
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "ActivePayload"
    ${If} $RcStatus == 2
    ${OrIf} $RcStatus == 3
        !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_INSTALL_STATE}" "PendingCleanup"
        ${If} $RcStatus == 2
        ${OrIf} $RcStatus == 3
            !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "FormatSignature" "${RCMUSIC_FORMAT_SIGNATURE}"
            !insertmacro RCMUSIC_UNINSTALL_MATCHING "${RCMUSIC_INSTALL_STATE}" "Product" "${RCMUSIC_INSTALL_PRODUCT}"
            DeleteRegKey /ifempty HKCU "${RCMUSIC_INSTALL_STATE}"
        ${EndIf}
    ${EndIf}
    ClearErrors
    Delete "$INSTDIR\Uninstall.exe"
    IfErrors rcmusic_uninstall_failed
    Delete "$INSTDIR\.rcmusic-install.ini"
    IfErrors rcmusic_uninstall_failed
    ; Unknown files and directories remain untouched, including user data.
    RMDir "$INSTDIR"
    !if ${RCMUSIC_ASSOCIATIONS_ENABLED} == 1
    !if "${RCMUSIC_REGISTRY_BASE}" == "Software"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    !endif
    !endif
rcmusic_uninstall_done:
    Call un.RCMusic.ReleaseMutex
    SetErrorLevel 0
    Goto rcmusic_uninstall_exit
rcmusic_uninstall_failed:
    Call un.RCMusic.ReleaseMutex
    SetErrorLevel 1
    ${If} $RcShortcutProblem == 1
        Abort "O atalho RC Music do menu Iniciar está modificado ou em uso. Verifique o atalho ou feche aplicações que o utilizem antes de tentar desinstalar novamente."
    ${EndIf}
    Abort "Não foi possível remover esta versão. Feche o RC Music e tente novamente; arquivos modificados não serão apagados."
rcmusic_uninstall_exit:
SectionEnd
