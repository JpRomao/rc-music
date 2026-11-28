import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraTracks"
    when: windowShown
    readonly property var contract: testProductUiContract // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    function cleanup() {
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }
    function sameColor(actual, expected, context) {
        verify(Qt.colorEqual(actual, expected), context + ": " + actual + " != " + expected);
    }
    function child(item, name) {
        const found = findChild(item, name);
        verify(found, "missing child " + name);
        return found;
    }
    function near(actual, expected, context) {
        verify(Math.abs(actual - expected) <= 0.5, context + ": " + actual + " != " + expected);
    }
    function xIn(row, item) {
        return item.mapToItem(row, 0, 0).x;
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }
    Component {
        id: rowHost
        Window {
            id: window
            property var props: ({})
            property alias row: hosted
            width: 900
            height: 200
            visible: true
            color: testCase.contract.canvasColor
            AuroraTrackRow {
                id: hosted
                contract: testCase.contract
                width: 900
                y: 20
                number: 3
                title: "Faixa de teste"
                subtitle: "Artista"
                album: "Álbum"
                durationText: "3:45"
                accessibleName: "Faixa de teste, Artista"
                Component.onCompleted: {
                    for (const key in window.props)
                        hosted[key] = window.props[key];
                }
            }
        }
    }
    Component {
        id: cardHost
        Window {
            id: window
            property var props: ({})
            property alias card: hosted
            width: 300
            height: 300
            visible: true
            color: testCase.contract.canvasColor
            AuroraTrackCard {
                id: hosted
                contract: testCase.contract
                x: 20
                y: 20
                title: "Faixa de teste"
                subtitle: "Artista"
                Component.onCompleted: {
                    for (const key in window.props)
                        hosted[key] = window.props[key];
                }
            }
        }
    }
    function makeRow(props) {
        const h = createTemporaryObject(rowHost, testCase, {
            props: props || {}
        });
        verify(h);
        verify(waitForRendering(h.contentItem));
        return h.row;
    }
    function makeCard(props) {
        const h = createTemporaryObject(cardHost, testCase, {
            props: props || {}
        });
        verify(h);
        verify(waitForRendering(h.contentItem));
        return h.card;
    }
    function spy(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
            target: target,
            signalName: signalName
        });
    }
    function center(item) {
        return item.mapToItem(null, item.width / 2, item.height / 2);
    }

    function test_rowMatchesTheMockup() {
        const row = makeRow();
        compare(row.height, 56);
        compare(child(row, "track-row-artwork").height, 40);
        compare(child(row, "track-row-artwork").width, 40);
        // 900 - 20 de padding - 4 espaços de 12 - 44 - 56 - 96 = 636 para as colunas flexíveis.
        const flex = 900 - 20 - 48 - 44 - 56 - 96;
        const titleWidth = flex * 1.4 / 2.4;
        const albumWidth = flex / 2.4;
        near(xIn(row, child(row, "track-row-artwork")), 10 + 44 + 12, "capa");
        const album = child(row, "track-row-album");
        near(xIn(row, album), 66 + titleWidth + 12, "álbum");
        near(album.width, albumWidth, "largura do álbum");
        const duration = child(row, "track-row-duration");
        near(xIn(row, duration), 66 + titleWidth + 12 + albumWidth + 12, "duração");
        near(duration.width, 56, "largura da duração");
        near(xIn(row, duration) + 56 + 12 + 96 + 10, 900, "coluna de ações");
    }
    function test_compactRowDropsTheAlbum() {
        const row = makeRow({
            compact: true
        });
        compare(row.height, 52);
        compare(child(row, "track-row-artwork").height, 36);
        verify(!child(row, "track-row-album").visible);
        const duration = child(row, "track-row-duration");
        near(xIn(row, duration) + 56 + 12 + 96 + 10, 900, "coluna de ações");
    }
    function test_rowShowsActionsOnHoverOrHighlight() {
        const row = makeRow();
        verify(child(row, "track-row-number").visible);
        verify(!child(row, "track-row-play").visible);
        verify(!child(row, "track-row-enqueue").visible);
        verify(!child(row, "track-row-more").visible);
        sameColor(child(row, "track-row-subtitle").color, contract.textSecondaryColor, "subtítulo em repouso");
        mouseMove(row, 300, 28);
        tryVerify(() => child(row, "track-row-play").visible);
        verify(child(row, "track-row-enqueue").visible);
        verify(child(row, "track-row-more").visible);
        verify(!child(row, "track-row-number").visible);
        sameColor(child(row, "track-row-subtitle").color, contract.textPrimaryColor, "subtítulo no hover");
        sameColor(child(row, "track-row-duration").color, contract.textPrimaryColor, "duração no hover");
        mouseMove(row.Window.window.contentItem, 1, 1);
        tryVerify(() => !child(row, "track-row-play").visible);
        row.highlighted = true;
        verify(child(row, "track-row-play").visible);
        verify(child(row, "track-row-more").visible);
        sameColor(child(row, "track-row-album").color, contract.textPrimaryColor, "álbum destacado");
    }
    function test_playingRowShowsTheWave() {
        const row = makeRow({
            current: true,
            playing: true
        });
        verify(child(row, "track-row-wave").visible);
        verify(!child(row, "track-row-number").visible);
        sameColor(child(row, "track-row-title").color, contract.accentColor, "título tocando");
        row.playing = false;
        verify(!child(row, "track-row-wave").visible);
        verify(child(row, "track-row-number").visible);
        sameColor(child(row, "track-row-title").color, contract.accentColor, "título atual");
    }
    function test_unavailableRowOffersLocate() {
        const row = makeRow({
            available: false
        });
        const unavailable = child(row, "track-row-unavailable");
        verify(unavailable.visible);
        compare(unavailable.text, "Indisponível");
        sameColor(unavailable.color, contract.warningColor, "aviso");
        mouseMove(row, 300, 28);
        tryVerify(() => child(row, "track-row-play").visible);
        verify(!child(row, "track-row-play").enabled);
        verify(child(row, "track-row-locate").visible);
        verify(!child(row, "track-row-enqueue").visible);
        verify(!child(row, "track-row-more").visible);
        const duration = child(row, "track-row-duration");
        verify(xIn(row, child(row, "track-row-locate")) >= xIn(row, duration) + duration.width, "Localizar invade a duração");
        for (const name of ["track-row-title", "track-row-subtitle", "track-row-unavailable", "track-row-album", "track-row-duration", "track-row-number"])
            compare(child(row, name).opacity, 1, name);
        compare(row.opacity, 1);
        mouseMove(row.Window.window.contentItem, 1, 1);
        tryVerify(() => !child(row, "track-row-play").visible);
        verify(child(row, "track-row-locate").visible);
        sameColor(child(row, "track-row-title").color, contract.textSecondaryColor, "título indisponível");
    }
    function test_rowSignals() {
        const row = makeRow();
        const activated = spy(row, "activated");
        const play = spy(row, "playRequested");
        const enqueue = spy(row, "enqueueRequested");
        const more = spy(row, "moreRequested");
        const menu = spy(row, "contextMenuRequested");
        mouseMove(row, 300, 28);
        tryVerify(() => child(row, "track-row-play").visible);
        mouseDoubleClickSequence(row, 300, 28);
        compare(activated.count, 1);
        for (const name of ["track-row-play", "track-row-enqueue", "track-row-more"])
            verify(!child(row, name).activeFocusOnTab, name);
        const root = row.Window.window.contentItem;
        let p = center(child(row, "track-row-play"));
        mouseClick(root, p.x, p.y);
        compare(play.count, 1);
        p = center(child(row, "track-row-enqueue"));
        mouseClick(root, p.x, p.y);
        compare(enqueue.count, 1);
        p = center(child(row, "track-row-more"));
        mouseClick(root, p.x, p.y);
        compare(more.count, 1);
        compare(more.signalArguments[0][0].objectName, "track-row-more");
        compare(activated.count, 1);
        mouseClick(row, 300, 28, Qt.RightButton);
        compare(menu.count, 1);
        compare(menu.signalArguments[0][0], row);
    }
    function test_unavailableLocateSignal() {
        const row = makeRow({
            available: false
        });
        const locate = spy(row, "locateRequested");
        verify(!child(row, "track-row-locate").activeFocusOnTab);
        const p = center(child(row, "track-row-locate"));
        mouseClick(row.Window.window.contentItem, p.x, p.y);
        compare(locate.count, 1);
        row.locateEnabled = false;
        verify(!child(row, "track-row-locate").enabled);
    }
    function test_cardOverlays() {
        const card = makeCard({
            actionsShown: true
        });
        const art = child(card, "shelf-card-artwork");
        const play = child(card, "shelf-card-play");
        const enqueue = child(card, "shelf-card-enqueue");
        const more = child(card, "shelf-card-more");
        verify(play.visible && enqueue.visible && more.visible);
        function rect(item) {
            const b = child(item, "icon-button-background");
            const o = b.mapToItem(art, 0, 0);
            return {
                x: o.x,
                y: o.y,
                w: b.width,
                h: b.height
            };
        }
        const pr = rect(play);
        near(pr.w, 40, "play");
        near(art.width - (pr.x + pr.w), 8, "play à direita");
        near(art.height - (pr.y + pr.h), 8, "play embaixo");
        const er = rect(enqueue);
        near(er.w, 40, "enqueue");
        near(art.width - (er.x + er.w), 54, "enqueue à direita");
        near(art.height - (er.y + er.h), 8, "enqueue embaixo");
        const mr = rect(more);
        near(mr.w, 32, "more");
        near(art.width - (mr.x + mr.w), 8, "more à direita");
        near(mr.y, 8, "more em cima");
        card.actionsShown = false;
        verify(!play.visible && !enqueue.visible && !more.visible);
        verify(!child(card, "shelf-card-unavailable").visible);
        card.unavailableBadge = true;
        const badge = child(card, "shelf-card-unavailable");
        verify(badge.visible);
        compare(child(badge, "chip-label").text, "Indisponível");
        sameColor(child(badge, "chip-label").color, contract.warningColor, "selo");
    }
    function test_cardClickSelectsAndDoubleClickActivates() {
        const card = makeCard({
            activateOnClick: false
        });
        const selected = spy(card, "selected");
        const activated = spy(card, "activated");
        mouseClick(card, 75, 75);
        compare(selected.count, 1);
        compare(activated.count, 0);
        mouseDoubleClickSequence(card, 75, 75);
        compare(activated.count, 1);
        const immediate = makeCard();
        const quick = spy(immediate, "activated");
        mouseClick(immediate, 75, 75);
        compare(quick.count, 1);
    }
    function test_cardActionSignals() {
        const card = makeCard({
            actionsShown: true
        });
        const play = spy(card, "playRequested");
        const enqueue = spy(card, "enqueueRequested");
        const more = spy(card, "moreRequested");
        const activated = spy(card, "activated");
        const root = card.Window.window.contentItem;
        for (const name of ["shelf-card-play", "shelf-card-enqueue", "shelf-card-more"]) {
            verify(!child(card, name).activeFocusOnTab, name);
            const p = center(child(card, name));
            mouseClick(root, p.x, p.y);
        }
        compare(play.count, 1);
        compare(enqueue.count, 1);
        compare(more.count, 1);
        compare(more.signalArguments[0][0].objectName, "shelf-card-more");
        compare(activated.count, 0);
    }
    function test_iconButtonVariants() {
        for (const dark of [false, true]) {
            driver.setSystemAppearance(dark, false);
            const card = makeCard({
                actionsShown: true
            });
            const play = child(card, "shelf-card-play");
            sameColor(child(play, "icon-button-background").color, contract.accentColor, "acento");
            sameColor(play.foregroundColor, contract.accentTextColor, "ícone do acento");
            const enqueue = child(card, "shelf-card-enqueue");
            sameColor(child(enqueue, "icon-button-background").color, contract.surfaceRaisedColor, "elevado");
        }
        const card = makeCard({
            actionsShown: true
        });
        const more = child(card, "shelf-card-more");
        compare(child(more, "icon-button-background").width, 32);
        compare(more.width, 40);
        compare(more.height, 40);
        compare(child(more, "icon-button-background").radius, 16);
        compare(child(child(card, "shelf-card-play"), "icon-button-background").width, 40);
    }
    function test_iconButtonVariantsInHighContrast() {
        driver.setHighContrast(true, false);
        const card = makeCard({
            actionsShown: true
        });
        for (const name of ["shelf-card-play", "shelf-card-enqueue", "shelf-card-more"]) {
            const bg = child(child(card, name), "icon-button-background");
            compare(bg.border.width, 2, name);
        }
        verify(!child(child(card, "shelf-card-enqueue"), "surface-shadow").visible);
    }
}
