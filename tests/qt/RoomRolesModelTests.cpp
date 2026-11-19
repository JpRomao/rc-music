// RoomRolesModel and RoomKnownPeopleModel (S10.1): the host's roles and known people for QML. No
// network, files or audio.
#include "m0/domain/RoomRoster.hpp"
#include "presentation/RoomRolesModel.hpp"

#include <QSignalSpy>
#include <QTest>

#include <vector>

namespace music_player::m0::test {
namespace {

RoomPermissionSet skipAndPause() {
    RoomPermissionSet set;
    set.set(static_cast<std::size_t>(RoomPermission::Skip));
    set.set(static_cast<std::size_t>(RoomPermission::Pause));
    return set;
}

QVariant cell(const QAbstractItemModel& model, const int row, const QByteArray& role) {
    const auto names = model.roleNames();
    return model.data(model.index(row, 0), names.key(role));
}

} // namespace

class RoomRolesModelTests final : public QObject {
    Q_OBJECT

  private slots:
    void rolesListEachRoleWithItsPermissionsAndTheInitialOne() {
        auto roster = defaultRoomRoster();
        roster.roles.push_back(RoomRole{3, "Amigos", skipAndPause()});
        roster.listeners.push_back(KnownListener{"fp-lia", "Lia", 3, {}, false});
        RoomRolesModel model;
        model.replace(roster, 1);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(cell(model, 2, "roleId").toInt(), 3);
        QCOMPARE(cell(model, 2, "name").toString(), QStringLiteral("Amigos"));
        QCOMPARE(cell(model, 2, "permissions").toInt(), 3);
        QCOMPARE(cell(model, 2, "holders").toInt(), 1);
        QVERIFY(!cell(model, 2, "initial").toBool());
        QVERIFY(cell(model, 0, "initial").toBool());
        QCOMPARE(cell(model, 1, "permissions").toInt(), (1 << room_permission_count) - 1);
        QCOMPARE(model.data(model.index(2, 0), Qt::DisplayRole).toString(), QStringLiteral("Amigos"));
    }

    void anEditKeepsTheRowsAndOnlyANewRoleListResetsThem() {
        auto roster = defaultRoomRoster();
        RoomRolesModel model;
        model.replace(roster, 1);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        roster.roles[0].permissions = skipAndPause();
        roster.roles[1].name = "Turma";
        model.replace(roster, 2);
        QCOMPARE(resets.size(), 0);
        QVERIFY(changes.size() >= 2);
        QCOMPARE(cell(model, 0, "permissions").toInt(), 3);
        QCOMPARE(cell(model, 1, "name").toString(), QStringLiteral("Turma"));
        QVERIFY(cell(model, 1, "initial").toBool());
        model.replace(roster, 2);
        QCOMPARE(changes.size(), 2);

        roster.roles.push_back(RoomRole{3, "Amigos", {}});
        QSignalSpy counts(&model, &RoomRolesModel::countChanged);
        model.replace(roster, 2);
        QCOMPARE(resets.size(), 1);
        QCOMPARE(counts.size(), 1);
        QCOMPARE(model.rowCount(), 3);
    }

    void knownPeopleShowNicknameRoleAndBlockButNeverTheFingerprint() {
        auto roster = defaultRoomRoster();
        roster.listeners.push_back(KnownListener{"fp-lia", "Lia", 2, {}, false});
        roster.listeners.push_back(KnownListener{"fp-caio", "Caio", 1, {}, true});
        AvatarChoice avatar;
        avatar.bundled_index = 3;
        RoomKnownPeopleModel model;
        model.replace(roster, {RoomMember{"fp-lia", "Lia", avatar, MemberState::Admitted}});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Lia"));
        QCOMPARE(cell(model, 0, "nickname").toString(), QStringLiteral("Lia"));
        QCOMPARE(cell(model, 0, "roleName").toString(), QStringLiteral("DJ"));
        QCOMPARE(cell(model, 0, "roleId").toInt(), 2);
        QVERIFY(cell(model, 0, "present").toBool());
        QVERIFY(!cell(model, 0, "blocked").toBool());
        QCOMPARE(cell(model, 0, "avatarSource").toUrl(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-04.svg")});
        QVERIFY(cell(model, 1, "blocked").toBool());
        QVERIFY(!cell(model, 1, "present").toBool());
        QCOMPARE(cell(model, 1, "avatarSource").toUrl(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-01.svg")});
        // The fingerprint is only an action key: no displayed role carries it.
        for (int row = 0; row < model.rowCount(); ++row)
            for (const auto& role : {Qt::DisplayRole, Qt::ToolTipRole, Qt::AccessibleTextRole})
                QVERIFY(!model.data(model.index(row, 0), role).toString().contains(QStringLiteral("fp-")));

        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        roster.listeners[1].blocked = false;
        model.replace(roster, {RoomMember{"fp-lia", "Lia", avatar, MemberState::Admitted}});
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 1);
        QVERIFY(!cell(model, 1, "blocked").toBool());
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::RoomRolesModelTests)
#include "RoomRolesModelTests.moc"
