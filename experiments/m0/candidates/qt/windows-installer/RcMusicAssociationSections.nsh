!ifndef RCMUSIC_ASSOCIATION_SECTIONS_INCLUDED
!define RCMUSIC_ASSOCIATION_SECTIONS_INCLUDED
!include "RcMusicAssociations.nsh"

; Insert after deployment has placed RCMusic.exe under $INSTDIR, and in the
; corresponding uninstaller before deleting the executable. Both must use
; RequestExecutionLevel user and SetShellVarContext current.
!macro RCMUSIC_INSTALL_ASSOCIATIONS
    Call RCMusic.RegisterAssociations
    Pop $0
    StrCmp $0 0 +3
        SetErrorLevel 1
        Abort "Não foi possível cadastrar as associações do RC Music."
    !if "${RCMUSIC_REGISTRY_BASE}" == "Software"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    !endif
!macroend

!macro RCMUSIC_UNINSTALL_ASSOCIATIONS
    Call un.RCMusic.UnregisterAssociations
    Pop $0
    StrCmp $0 0 +3
        SetErrorLevel 1
        Abort "Não foi possível remover as associações do RC Music."
    !if "${RCMUSIC_REGISTRY_BASE}" == "Software"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    !endif
!macroend

!endif
