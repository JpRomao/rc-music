import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0 as M0
import "../../qml" as Shell

TestCase {
    id: testCase
    name: "PreferencePersistenceNotice"
    when: windowShown

    Component {
        id: hostComponent
        ApplicationWindow {
            id: host
            width: 720
            height: 520
            visible: true
            property int requestedWarning: M0.PlayerPresenter.NoPreferenceWarning
            property bool allowInteraction: true
            property bool preferencesOpened: false
            property int dismissCount: 0
            property int retryCount: 0
            property int replacementCount: 0
            property alias persistenceNotice: notice
            property alias fallbackItem: fallback
            property alias competingItem: competing

            Button {
                id: fallback
                objectName: "preference-notice-fallback"
                text: "Preferências"
            }
            Button {
                id: competing
                objectName: "preference-notice-competing-focus"
                anchors.right: parent.right
                text: "Outro modal"
            }
            Shell.PreferencePersistenceNotice {
                id: notice
                x: 24
                y: 160
                width: parent.width - 48
                contract: testShellUiContract // qmllint disable unqualified
                warning: host.requestedWarning
                fallbackFocusItem: fallback
                interactionAllowed: host.allowInteraction
                onReviewRequested: host.preferencesOpened = true
                onDismissRequested: {
                    ++host.dismissCount;
                    host.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
                }
                onRetryRequested: ++host.retryCount
                onReplacementConfirmed: ++host.replacementCount
            }
        }
    }

    function createHost(warning) {
        const host = createTemporaryObject(hostComponent, null, {
            requestedWarning: warning
        });
        verify(host);
        host.requestActivate();
        tryVerify(function () {
            return host.active;
        });
        return host;
    }

    function verifySafePlainText(item) {
        compare(item.textFormat, Text.PlainText);
        verify(!item.text.includes("C:"));
        verify(!item.text.includes("\\private"));
        verify(!item.text.toLowerCase().includes("parser"));
        verify(!item.text.toLowerCase().includes(".json"));
    }

    function test_recoveryWarningsOfferReviewAndDismiss_data() {
        return [
            {
                tag: "backup",
                warning: M0.PlayerPresenter.RecoveredFromBackup,
                title: "Preferências recuperadas"
            },
            {
                tag: "defaults",
                warning: M0.PlayerPresenter.UsingDefaults,
                title: "Preferências padrão restauradas"
            }
        ];
    }

    function test_recoveryWarningsOfferReviewAndDismiss(data) {
        const host = createHost(data.warning);
        const notice = findChild(host, "preference-persistence-notice");
        const title = findChild(host, "preference-persistence-title");
        const message = findChild(host, "preference-persistence-message");
        tryVerify(function () {
            return notice.visible;
        });
        compare(notice.Accessible.role, Accessible.AlertMessage);
        compare(title.text, data.title);
        verifySafePlainText(title);
        verifySafePlainText(message);
        verify(notice.Accessible.name.includes(title.text));
        verify(notice.Accessible.name.includes(message.text));

        const review = findChild(host, "preference-persistence-review");
        const dismiss = findChild(host, "preference-persistence-dismiss");
        verify(review.visible && review.enabled);
        verify(dismiss.visible && dismiss.enabled);
        compare(review.Accessible.role, Accessible.Button);
        compare(review.Accessible.name, "Revisar preferências");
        review.Accessible.pressAction();
        compare(host.preferencesOpened, true);
        verify(notice.visible);

        dismiss.forceActiveFocus(Qt.TabFocusReason);
        verify(dismiss.activeFocus);
        keyClick(Qt.Key_Return);
        compare(host.dismissCount, 1);
        tryVerify(function () {
            return !notice.visible && host.fallbackItem.activeFocus;
        });
    }

    function test_asyncWarningResolutionRestoresFocusWithoutStealingIt() {
        const ownedHost = createHost(M0.PlayerPresenter.SaveFailed);
        const ownedRetry = findChild(ownedHost, "preference-persistence-retry");
        ownedRetry.forceActiveFocus(Qt.TabFocusReason);
        verify(ownedRetry.activeFocus);
        ownedHost.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
        tryVerify(function () {
            return !ownedHost.persistenceNotice.visible && ownedHost.fallbackItem.activeFocus;
        });

        const competingHost = createHost(M0.PlayerPresenter.SaveFailed);
        const competingRetry = findChild(competingHost, "preference-persistence-retry");
        competingRetry.forceActiveFocus(Qt.TabFocusReason);
        verify(competingRetry.activeFocus);
        competingHost.competingItem.forceActiveFocus(Qt.TabFocusReason);
        competingHost.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
        tryVerify(function () {
            return !competingHost.persistenceNotice.visible && competingHost.competingItem.activeFocus;
        });

        const inactiveHost = createHost(M0.PlayerPresenter.SaveFailed);
        const inactiveRetry = findChild(inactiveHost, "preference-persistence-retry");
        inactiveRetry.forceActiveFocus(Qt.TabFocusReason);
        verify(inactiveRetry.activeFocus);
        inactiveHost.allowInteraction = false;
        inactiveHost.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
        wait(0);
        verify(!inactiveHost.persistenceNotice.visible);
        inactiveHost.allowInteraction = true;
        tryVerify(function () {
            return inactiveHost.fallbackItem.activeFocus;
        });
    }

    function test_saveFailureOffersReviewAndRetryWithoutDismiss() {
        const host = createHost(M0.PlayerPresenter.SaveFailed);
        const notice = findChild(host, "preference-persistence-notice");
        const review = findChild(host, "preference-persistence-review");
        const retry = findChild(host, "preference-persistence-retry");
        verify(notice.visible);
        verify(review.visible && review.enabled);
        verify(retry.visible && retry.enabled);
        verify(!findChild(host, "preference-persistence-dismiss").visible);
        verify(!findChild(host, "preference-persistence-replace").visible);
        verifySafePlainText(findChild(host, "preference-persistence-message"));

        review.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return retry.activeFocus;
        });
        keyClick(Qt.Key_Return);
        compare(host.retryCount, 1);
        verify(notice.visible);
        review.Accessible.pressAction();
        compare(host.preferencesOpened, true);
    }

    function test_futureVersionRequiresExplicitReplacementConfirmation() {
        const host = createHost(M0.PlayerPresenter.FutureVersion);
        const replace = findChild(host, "preference-persistence-replace");
        verify(replace.visible && replace.enabled);
        verify(!findChild(host, "preference-persistence-dismiss").visible);
        verify(!findChild(host, "preference-persistence-retry").visible);

        replace.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const confirmation = findChild(host, "preference-replacement-confirmation");
        const cancel = findChild(host, "preference-replacement-cancel");
        const confirm = findChild(host, "preference-replacement-confirm");
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });
        compare(confirmation.modal, true);
        compare(confirmation.contentItem.Accessible.role, Accessible.Dialog);
        verifySafePlainText(findChild(host, "preference-replacement-message"));
        compare(host.replacementCount, 0);

        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return confirm.activeFocus;
        });
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return cancel.activeFocus;
        });
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryVerify(function () {
            return confirm.activeFocus;
        });

        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !confirmation.visible && replace.activeFocus;
        });
        compare(host.replacementCount, 0);

        replace.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });
        confirm.Accessible.pressAction();
        compare(host.replacementCount, 1);
        tryVerify(function () {
            return !confirmation.visible;
        });
    }

    function test_confirmationNeverStealsFocusAcrossStateOrModalChanges() {
        const host = createHost(M0.PlayerPresenter.FutureVersion);
        const replace = findChild(host, "preference-persistence-replace");
        const confirmation = findChild(host, "preference-replacement-confirmation");

        replace.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened;
        });
        host.competingItem.forceActiveFocus(Qt.TabFocusReason);
        host.allowInteraction = false;
        tryVerify(function () {
            return !confirmation.visible && host.competingItem.activeFocus;
        });
        compare(host.replacementCount, 0);

        host.allowInteraction = true;
        replace.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened;
        });
        host.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
        tryVerify(function () {
            return !confirmation.visible && host.fallbackItem.activeFocus;
        });
        compare(host.replacementCount, 0);
    }

    function test_warningIsAnnouncedWithoutLeakingDiagnostics() {
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        const host = createHost(M0.PlayerPresenter.NoPreferenceWarning);
        host.requestedWarning = M0.PlayerPresenter.SaveFailed;
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 1; // qmllint disable unqualified
        });
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive); // qmllint disable unqualified
        const announcement = testScenarioDriver.lastAccessibleAnnouncement(); // qmllint disable unqualified
        verify(announcement.includes("preferências"));
        verify(!announcement.includes("C:"));
        verify(!announcement.toLowerCase().includes("parser"));
        verify(!announcement.toLowerCase().includes(".json"));
    }

    function test_initialRecoveryWarningIsAnnouncedOnceAndPolitely() {
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        createHost(M0.PlayerPresenter.RecoveredFromBackup);
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 1; // qmllint disable unqualified
        });
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Polite); // qmllint disable unqualified
    }

    function test_warningIsAnnouncedAgainAfterSynchronousClearAndFailure() {
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        const host = createHost(M0.PlayerPresenter.NoPreferenceWarning);
        host.requestedWarning = M0.PlayerPresenter.SaveFailed;
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 1; // qmllint disable unqualified
        });

        host.requestedWarning = M0.PlayerPresenter.NoPreferenceWarning;
        host.requestedWarning = M0.PlayerPresenter.SaveFailed;
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 2; // qmllint disable unqualified
        });
    }

    function test_highContrastSurfacesRemainOpaque() {
        testScenarioDriver.setHighContrast(true, false); // qmllint disable unqualified
        const host = createHost(M0.PlayerPresenter.FutureVersion);
        const background = findChild(host, "preference-persistence-background");
        compare(background.color.a, 1);
        compare(background.border.width, 2);

        findChild(host, "preference-persistence-replace").Accessible.pressAction();
        const confirmationBackground = findChild(host, "preference-replacement-background");
        tryVerify(function () {
            return confirmationBackground.visible;
        });
        compare(confirmationBackground.color.a, 1);
        compare(confirmationBackground.border.width, 2);
        testScenarioDriver.setHighContrast(false, false); // qmllint disable unqualified
    }

    function test_minimumWindowKeepsEveryBannerActionVisible() {
        const host = createHost(M0.PlayerPresenter.FutureVersion);
        const notice = findChild(host, "preference-persistence-notice");
        const review = findChild(host, "preference-persistence-review");
        const replace = findChild(host, "preference-persistence-replace");
        verify(notice.width > 0 && notice.height > 0);
        verify(review.width > 0 && review.height > 0);
        verify(replace.width > 0 && replace.height > 0);
        const noticePosition = notice.mapToItem(host.contentItem, 0, 0);
        const reviewPosition = review.mapToItem(host.contentItem, 0, 0);
        const replacePosition = replace.mapToItem(host.contentItem, 0, 0);
        verify(noticePosition.x >= 0 && noticePosition.y >= 0);
        verify(noticePosition.x + notice.width <= host.contentItem.width);
        verify(noticePosition.y + notice.height <= host.contentItem.height);
        verify(reviewPosition.x >= noticePosition.x && reviewPosition.y >= noticePosition.y);
        verify(replacePosition.x + replace.width <= noticePosition.x + notice.width);
        verify(replacePosition.y + replace.height <= noticePosition.y + notice.height);
    }
}
