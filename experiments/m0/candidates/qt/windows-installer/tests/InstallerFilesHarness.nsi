; Isolated native probe for the shared installer/uninstaller helper.
Unicode true
RequestExecutionLevel user
SilentInstall silent
SilentUnInstall silent
Name "RC Music installer files contract"
OutFile "${RCMUSIC_TEST_OUTPUT}"
!addincludedir ".."
!include "RcMusicInstallerFiles.nsh"
!insertmacro RCMUSIC_INSTALLER_FILE_FUNCTIONS ""
!insertmacro RCMUSIC_INSTALLER_FILE_FUNCTIONS "un."
Var ProbeCase
Var ProbeArguments
Var ProbePreserved
Var ProbeOriginalPath
Var ProbeFile
Var ProbeTicks

!macro RCMUSIC_RUN_PROBE PREFIX
Function ${PREFIX}RunProbe
    ${GetParameters} $ProbeArguments
    ${GetOptions} $ProbeArguments "/CASE=" $ProbeCase
    StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\source ação 日本.txt"
    ${If} $ProbeCase == "empty"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\empty.txt"
    ${ElseIf} $ProbeCase == "large"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\large.bin"
    ${ElseIf} $ProbeCase == "missing"
    ${OrIf} $ProbeCase == "validate-missing"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\missing\absent.txt"
    ${ElseIf} $ProbeCase == "directory"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}"
    ${ElseIf} $ProbeCase == "readonly"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\readonly.txt"
    ${ElseIf} $ProbeCase == "reparse"
    ${OrIf} $ProbeCase == "validate-reparse"
        StrCpy $RcPath "${RCMUSIC_TEST_DIRECTORY}\junction\source ação 日本.txt"
    ${ElseIf} $ProbeCase != "hash"
    ${AndIf} $ProbeCase != "validate"
    ${AndIf} $ProbeCase != "mutex"
    ${AndIf} $ProbeCase != "mutex-hold"
    ${AndIf} $ProbeCase != "mutex-cycle"
        SetErrorLevel 1
        Return
    ${EndIf}
    StrCpy $0 "preserve-0"
    StrCpy $1 "preserve-1"
    StrCpy $2 "preserve-2"
    StrCpy $3 "preserve-3"
    StrCpy $4 "preserve-4"
    StrCpy $5 "preserve-5"
    StrCpy $6 "preserve-6"
    StrCpy $7 "preserve-7"
    StrCpy $8 "preserve-8"
    StrCpy $9 "preserve-9"
    StrCpy $ProbeOriginalPath $RcPath
    ${If} $ProbeCase == "validate"
    ${OrIf} $ProbeCase == "validate-missing"
    ${OrIf} $ProbeCase == "validate-reparse"
        Call ${PREFIX}RCMusic.ValidatePath
    ${ElseIf} $ProbeCase == "mutex"
    ${OrIf} $ProbeCase == "mutex-hold"
    ${OrIf} $ProbeCase == "mutex-cycle"
        Call ${PREFIX}RCMusic.AcquireMutex
    ${Else}
        Call ${PREFIX}RCMusic.HashFile
    ${EndIf}
    StrCpy $ProbePreserved 1
    ${If} $0 != "preserve-0"
    ${OrIf} $1 != "preserve-1"
    ${OrIf} $2 != "preserve-2"
    ${OrIf} $3 != "preserve-3"
    ${OrIf} $4 != "preserve-4"
    ${OrIf} $5 != "preserve-5"
    ${OrIf} $6 != "preserve-6"
    ${OrIf} $7 != "preserve-7"
    ${OrIf} $8 != "preserve-8"
    ${OrIf} $9 != "preserve-9"
    ${OrIf} $RcPath != $ProbeOriginalPath
        StrCpy $ProbePreserved 0
    ${EndIf}
    FileOpen $ProbeFile "${RCMUSIC_TEST_DIRECTORY}\report-$ProbeCase.txt" w
    FileWrite $ProbeFile "status=$RcStatus$\r$\nhash=$RcHash$\r$\npreserved=$ProbePreserved$\r$\n"
    FileClose $ProbeFile
    ${If} $ProbeCase == "mutex-hold"
        ; Publish readiness only after the result file is closed.
        FileOpen $ProbeFile "${RCMUSIC_TEST_DIRECTORY}\holder.ready" w
        FileClose $ProbeFile
    ${EndIf}
    ${If} $ProbeCase == "mutex-hold"
    ${AndIf} $RcStatus == 0
        StrCpy $ProbeTicks 0
        ${Do}
            IfFileExists "${RCMUSIC_TEST_DIRECTORY}\release.flag" hold_done
            Sleep 25
            IntOp $ProbeTicks $ProbeTicks + 1
        ${LoopWhile} $ProbeTicks < 400
hold_done:
    ${EndIf}
    ${If} $ProbeCase == "mutex"
    ${OrIf} $ProbeCase == "mutex-hold"
    ${OrIf} $ProbeCase == "mutex-cycle"
        Call ${PREFIX}RCMusic.ReleaseMutex
        ${If} $ProbeCase == "mutex-cycle"
            Call ${PREFIX}RCMusic.ReleaseMutex
            FileOpen $ProbeFile "${RCMUSIC_TEST_DIRECTORY}\release-status.txt" w
            FileWrite $ProbeFile "$RcStatus"
            FileClose $ProbeFile
        ${EndIf}
    ${EndIf}
    SetErrorLevel 0
FunctionEnd
!macroend
!insertmacro RCMUSIC_RUN_PROBE ""
!insertmacro RCMUSIC_RUN_PROBE "un."

Section
    ${GetParameters} $ProbeArguments
    ${GetOptions} $ProbeArguments "/CASE=" $ProbeCase
    ${If} $ProbeCase == "bootstrap"
        WriteUninstaller "${RCMUSIC_TEST_DIRECTORY}\Uninstall.exe"
    ${Else}
        Call RunProbe
    ${EndIf}
SectionEnd

Section "Uninstall"
    Call un.RunProbe
SectionEnd
