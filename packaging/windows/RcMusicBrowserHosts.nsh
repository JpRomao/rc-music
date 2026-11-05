!ifndef RCMUSIC_BROWSER_HOSTS_INCLUDED
!define RCMUSIC_BROWSER_HOSTS_INCLUDED

; Caller owns the selected-channel inventory, strict REG_SZ reader, payload
; transaction and installer mutex. No extension policy/profile is configured.
; The strict reader and these callbacks may use $0-$5 and $RcValue as scratch.
!macro RCMUSIC_BROWSER_HOST_CHECK REGISTRY_SUFFIX MANIFEST_RELATIVE
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}" ""
    ${If} $RcStatus == 0
        ${If} $RcValue S!= "$INSTDIR\${MANIFEST_RELATIVE}"
            ${If} $RcActive == ""
                StrCpy $RcStatus 1
                Return
            ${EndIf}
            ${If} $RcValue S!= "${RCMUSIC_INSTALL_ROOT}\$RcActive\${MANIFEST_RELATIVE}"
                StrCpy $RcStatus 1
                Return
            ${EndIf}
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        StrCpy $RcStatus 1
        Return
    ${EndIf}
!macroend

!macro RCMUSIC_BROWSER_HOST_PREFLIGHT REGISTRY_SUFFIX MANIFEST_RELATIVE
    !insertmacro RCMUSIC_BROWSER_HOST_CHECK "${REGISTRY_SUFFIX}" "${MANIFEST_RELATIVE}"
!macroend

!macro RCMUSIC_BROWSER_HOST_REGISTER REGISTRY_SUFFIX MANIFEST_RELATIVE
    ; Recheck each value immediately before replacing an absent or owned target.
    !insertmacro RCMUSIC_BROWSER_HOST_CHECK "${REGISTRY_SUFFIX}" "${MANIFEST_RELATIVE}"
    ClearErrors
    WriteRegStr HKCU "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}" "" "$INSTDIR\${MANIFEST_RELATIVE}"
    ${If} ${Errors}
        StrCpy $RcStatus 1
        Return
    ${EndIf}
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}" ""
    ${If} $RcStatus != 0
        StrCpy $RcStatus 1
        Return
    ${EndIf}
    ${If} $RcValue S!= "$INSTDIR\${MANIFEST_RELATIVE}"
        StrCpy $RcStatus 1
        Return
    ${EndIf}
!macroend

!macro RCMUSIC_BROWSER_HOST_UNREGISTER REGISTRY_SUFFIX MANIFEST_RELATIVE
    !insertmacro RCMUSIC_INSTALL_READ "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}" ""
    ${If} $RcStatus == 0
        ${If} $RcValue S== "$INSTDIR\${MANIFEST_RELATIVE}"
            ClearErrors
            DeleteRegValue HKCU "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}" ""
            ${If} ${Errors}
                StrCpy $RcStatus 1
                Return
            ${EndIf}

            ; Preserve any remaining foreign values/subkeys. Query failures must
            ; not be confused with an empty key or permission to delete payload.
            StrCpy $0 "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}"
            System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w r0, i 0, i 0x9, *p .r1) i.r2'
            ${If} $2 == 0
                System::Call 'advapi32::RegQueryInfoKeyW(p r1, p 0, p 0, p 0, *i .r3, p 0, p 0, *i .r4, p 0, p 0, p 0, p 0) i.r2'
                System::Call 'advapi32::RegCloseKey(p r1) i.r5'
                ${If} $2 != 0
                ${OrIf} $5 != 0
                    StrCpy $RcStatus 1
                    Return
                ${EndIf}
                ${If} $3 == 0
                ${AndIf} $4 == 0
                    ClearErrors
                    DeleteRegKey /ifempty HKCU "${RCMUSIC_REGISTRY_BASE}\${REGISTRY_SUFFIX}"
                    ${If} ${Errors}
                        StrCpy $RcStatus 1
                        Return
                    ${EndIf}
                ${EndIf}
            ${ElseIf} $2 != 2
            ${AndIf} $2 != 3
                StrCpy $RcStatus 1
                Return
            ${EndIf}
        ${EndIf}
    ${ElseIf} $RcStatus != 2
    ${AndIf} $RcStatus != 3
        StrCpy $RcStatus 1
        Return
    ${EndIf}
!macroend

Function RCMusic.PreflightBrowserHosts
    !insertmacro RCMUSIC_BROWSER_HOSTS RCMUSIC_BROWSER_HOST_PREFLIGHT
    StrCpy $RcStatus 0
FunctionEnd

Function RCMusic.RegisterBrowserHosts
    !insertmacro RCMUSIC_BROWSER_HOSTS RCMUSIC_BROWSER_HOST_REGISTER
    StrCpy $RcStatus 0
FunctionEnd

Function un.RCMusic.UnregisterBrowserHosts
    !insertmacro RCMUSIC_BROWSER_HOSTS RCMUSIC_BROWSER_HOST_UNREGISTER
    StrCpy $RcStatus 0
FunctionEnd

!endif
