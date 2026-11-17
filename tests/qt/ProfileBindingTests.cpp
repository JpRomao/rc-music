#include "presentation/ProfileBinding.hpp"

#include "m0/domain/ListenerProfile.hpp"
#include "m0/ports/ProfileStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>
#include <vector>

using namespace music_player::m0;
namespace {
class FakeProfileStore final : public ProfileStore {
  public:
    ProfileLoadResult load() override { return loaded; }
    ProfileSaveStatus save(const ListenerProfile& profile) override {
        ++save_attempts;
        if (next_save == ProfileSaveStatus::Saved)
            saved.push_back(profile);
        return next_save;
    }
    ProfileLoadResult loaded{};
    ProfileSaveStatus next_save{ProfileSaveStatus::Saved};
    std::vector<ListenerProfile> saved{};
    int save_attempts{0};
};

constexpr std::uint64_t random_value = 47; // 47 % 20 == 7

std::unique_ptr<ProfileBinding> makeBinding(const std::shared_ptr<FakeProfileStore>& store,
                                            const QString& config_directory) {
    return std::make_unique<ProfileBinding>(store, config_directory, [] { return random_value; });
}

std::shared_ptr<FakeProfileStore> loadedStore(ListenerProfile profile) {
    auto store = std::make_shared<FakeProfileStore>();
    store->loaded = {std::move(profile), ProfileLoadStatus::Loaded};
    return store;
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// Minimal 1x1 GIF89a, crafted by hand because the gif writer plugin may not be deployed.
const QByteArray gif_bytes = QByteArray::fromHex("47494638396101000100800000000000ffffff21f90401000000002c00000000"
                                                 "010001000002024401003b");
} // namespace

class ProfileBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void withoutProfileNeedsSetupWithDrawnAvatarAndWritesNothing() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(binding->needsSetup());
        QVERIFY(binding->available());
        QCOMPARE(binding->nickname(), QString{});
        QCOMPARE(binding->avatarIndex(), static_cast<int>(initialAvatar(random_value).bundled_index));
        QCOMPARE(binding->avatarSource(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-08.svg")});
        QCOMPARE(store->save_attempts, 0);
    }
    void setNicknameSavesTheCanonicalProfileAndEndsSetup() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        const auto binding = makeBinding(store, dir.path());
        QSignalSpy changed(binding.get(), &ProfileBinding::changed);
        QVERIFY(binding->setNickname(QStringLiteral("  Ana ")));
        QVERIFY(!binding->needsSetup());
        QCOMPARE(binding->nickname(), QStringLiteral("Ana"));
        QCOMPARE(store->saved.size(), std::size_t{1});
        QCOMPARE(store->saved.back().nickname, std::string{"Ana"});
        QCOMPARE(store->saved.back().avatar, initialAvatar(random_value));
        QVERIFY(!changed.isEmpty());
    }
    void rejectsInvalidNicknamesWithoutWriting() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        const auto binding = makeBinding(store, dir.path());
        for (const auto& nickname : {QString{}, QStringLiteral("   "), QStringLiteral("Ana  Bia"),
                                     QStringLiteral("Ana\tBia"), QString(33, QLatin1Char{'a'})}) {
            QVERIFY2(!binding->acceptsNickname(nickname), qPrintable(nickname));
            QVERIFY2(!binding->setNickname(nickname), qPrintable(nickname));
        }
        QVERIFY(binding->acceptsNickname(QStringLiteral(" Ana Bia ")));
        QVERIFY(binding->needsSetup());
        QCOMPARE(store->save_attempts, 0);
    }
    void failedSaveKeepsAskingForTheNickname() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        store->next_save = ProfileSaveStatus::Failed;
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(!binding->setNickname(QStringLiteral("Ana")));
        QVERIFY(binding->needsSetup());
        QCOMPARE(binding->nickname(), QString{});
    }
    void loadedProfileDoesNotNeedSetup() {
        QTemporaryDir dir;
        const auto binding = makeBinding(loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}}), dir.path());
        QVERIFY(!binding->needsSetup());
        QVERIFY(binding->available());
        QCOMPARE(binding->nickname(), QStringLiteral("Bia"));
        QCOMPARE(binding->avatarIndex(), 4);
        QCOMPARE(binding->avatarSource(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-05.svg")});
    }
    void invalidProfileWithoutBackupNeedsSetup() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        store->loaded = {std::nullopt, ProfileLoadStatus::Invalid};
        QVERIFY(makeBinding(store, dir.path())->needsSetup());
        store->loaded = {ListenerProfile{"Bia", {AvatarKind::Bundled, 1, {}}}, ProfileLoadStatus::RecoveredFromBackup};
        QVERIFY(!makeBinding(store, dir.path())->needsSetup());
    }
    void futureOrUnavailableProfileNeverAsksAndNeverWrites() {
        for (const auto status : {ProfileLoadStatus::FutureVersion, ProfileLoadStatus::Unavailable}) {
            QTemporaryDir dir;
            auto store = std::make_shared<FakeProfileStore>();
            store->loaded = {std::nullopt, status};
            const auto binding = makeBinding(store, dir.path());
            QVERIFY(!binding->needsSetup());
            QVERIFY(!binding->available());
            QVERIFY(!binding->setNickname(QStringLiteral("Ana")));
            QVERIFY(!binding->chooseBundledAvatar(2));
            QVERIFY(!binding->chooseCustomAvatar(QUrl::fromLocalFile(dir.filePath(QStringLiteral("a.png")))));
            QCOMPARE(store->save_attempts, 0);
        }
    }
    void choosingABundledAvatarDuringSetupWaitsForTheNickname() {
        QTemporaryDir dir;
        auto store = std::make_shared<FakeProfileStore>();
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(binding->chooseBundledAvatar(3));
        QCOMPARE(binding->avatarIndex(), 3);
        QCOMPARE(store->save_attempts, 0);
        QVERIFY(!binding->chooseBundledAvatar(20));
        QVERIFY(!binding->chooseBundledAvatar(-1));
        QCOMPARE(binding->avatarIndex(), 3);
        QVERIFY(binding->setNickname(QStringLiteral("Ana")));
        QCOMPARE(store->saved.back().avatar, (AvatarChoice{AvatarKind::Bundled, 3, {}}));
    }
    void choosingABundledAvatarAfterSetupSavesIt() {
        QTemporaryDir dir;
        auto store = loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}});
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(binding->chooseBundledAvatar(19));
        QCOMPARE(binding->avatarIndex(), 19);
        QCOMPARE(binding->avatarSource(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-20.svg")});
        QCOMPARE(store->saved.back(), (ListenerProfile{"Bia", {AvatarKind::Bundled, 19, {}}}));
        store->next_save = ProfileSaveStatus::Failed;
        QVERIFY(!binding->chooseBundledAvatar(0));
        QCOMPARE(binding->avatarIndex(), 19);
    }
    void changingTheNicknameLaterKeepsTheAvatar() {
        QTemporaryDir dir;
        auto store = loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}});
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(binding->setNickname(QStringLiteral("Bia Lima")));
        QCOMPARE(binding->nickname(), QStringLiteral("Bia Lima"));
        QCOMPARE(store->saved.back(), (ListenerProfile{"Bia Lima", {AvatarKind::Bundled, 4, {}}}));
    }
    void chooseCustomAvatarRejectsAGifAndKeepsTheAvatar() {
        QTemporaryDir dir;
        auto store = loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}});
        const auto binding = makeBinding(store, dir.path());
        const auto gif = dir.filePath(QStringLiteral("avatar.gif"));
        QVERIFY(writeBytes(gif, gif_bytes));
        QVERIFY(!binding->chooseCustomAvatar(QUrl::fromLocalFile(gif)));
        QCOMPARE(binding->avatarIndex(), 4);
        QCOMPARE(binding->avatarSource(), QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-05.svg")});
        QCOMPARE(store->save_attempts, 0);
        QVERIFY(!QFileInfo::exists(QDir{dir.path()}.filePath(QStringLiteral("avatars/custom.png"))));
    }
    void chooseCustomAvatarRejectsRemoteUrls() {
        QTemporaryDir dir;
        auto store = loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}});
        const auto binding = makeBinding(store, dir.path());
        QVERIFY(!binding->chooseCustomAvatar(QUrl{QStringLiteral("https://example.invalid/a.png")}));
        QCOMPARE(store->save_attempts, 0);
    }
    void chooseCustomAvatarStoresTheReducedImage() {
        QTemporaryDir dir;
        auto store = loadedStore({"Bia", {AvatarKind::Bundled, 4, {}}});
        const auto binding = makeBinding(store, dir.path());
        const auto png = dir.filePath(QStringLiteral("photo.png"));
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(QColor(20, 120, 60));
        QVERIFY(image.save(png, "PNG"));
        QVERIFY(binding->chooseCustomAvatar(QUrl::fromLocalFile(png)));
        QCOMPARE(binding->avatarIndex(), -1);
        const auto custom = QDir{dir.path()}.filePath(QStringLiteral("avatars/custom.png"));
        QCOMPARE(QFileInfo{binding->avatarSource().toLocalFile()}.absoluteFilePath(),
                 QFileInfo{custom}.absoluteFilePath());
        QVERIFY(binding->avatarSource().isLocalFile());
        QCOMPARE(store->saved.back().avatar.kind, AvatarKind::Custom);
        QCOMPARE(store->saved.back().avatar.custom_sha256.size(), std::size_t{64});
        QCOMPARE(store->saved.back().nickname, std::string{"Bia"});
    }
    void loadedCustomAvatarPointsAtTheSavedFile() {
        QTemporaryDir dir;
        const std::string sha(64, 'a');
        const auto binding = makeBinding(loadedStore({"Bia", {AvatarKind::Custom, 0, sha}}), dir.path());
        QCOMPARE(binding->avatarIndex(), -1);
        QVERIFY(binding->avatarSource().isLocalFile());
        QCOMPARE(QFileInfo{binding->avatarSource().toLocalFile()}.absoluteFilePath(),
                 QFileInfo{QDir{dir.path()}.filePath(QStringLiteral("avatars/custom.png"))}.absoluteFilePath());
    }
};

QTEST_GUILESS_MAIN(ProfileBindingTests)
#include "ProfileBindingTests.moc"
