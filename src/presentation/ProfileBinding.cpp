#include "presentation/ProfileBinding.hpp"

#include "adapters/QtAvatarImage.hpp"

#include <QDir>
#include <QRandomGenerator>

#include <utility>

namespace music_player::m0 {
namespace {
[[nodiscard]] std::optional<std::string> canonical(const QString& nickname) {
    return canonicalNickname(nickname.toStdString());
}
} // namespace

ProfileBinding::ProfileBinding(std::shared_ptr<ProfileStore> store, QString config_directory, RandomSource random,
                               QObject* parent)
    : QObject(parent), store_(std::move(store)), config_directory_(std::move(config_directory)) {
    auto loaded = store_->load();
    switch (loaded.status) {
    case ProfileLoadStatus::Loaded:
    case ProfileLoadStatus::RecoveredFromBackup:
        if (loaded.profile.has_value()) {
            profile_ = std::move(*loaded.profile);
            return;
        }
        break;
    case ProfileLoadStatus::FutureVersion:
    case ProfileLoadStatus::Unavailable:
        // Never ask again or overwrite a file this build cannot read.
        available_ = false;
        return;
    case ProfileLoadStatus::Missing:
    case ProfileLoadStatus::Invalid:
        break;
    }
    needs_setup_ = true;
    profile_.avatar = initialAvatar(random ? random() : QRandomGenerator::system()->generate64());
}

bool ProfileBinding::needsSetup() const noexcept { return needs_setup_; }
bool ProfileBinding::available() const noexcept { return available_; }
QString ProfileBinding::nickname() const { return QString::fromStdString(profile_.nickname); }

QUrl ProfileBinding::avatarSource() const {
    if (!available_)
        return {};
    if (profile_.avatar.kind == AvatarKind::Custom) {
        // The digest in the query busts the image cache when the same file is replaced.
        auto url = QUrl::fromLocalFile(QDir{config_directory_}.filePath(QStringLiteral("avatars/custom.png")));
        url.setQuery(QStringLiteral("v=") + QString::fromStdString(profile_.avatar.custom_sha256));
        return url;
    }
    return QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-%1.svg")
                    .arg(profile_.avatar.bundled_index + 1, 2, 10, QLatin1Char{'0'})};
}

int ProfileBinding::avatarIndex() const noexcept {
    return available_ && profile_.avatar.kind == AvatarKind::Bundled ? static_cast<int>(profile_.avatar.bundled_index)
                                                                     : -1;
}

int ProfileBinding::avatarCount() noexcept { return static_cast<int>(bundled_avatar_count); }

bool ProfileBinding::acceptsNickname(const QString& nickname) const { return canonical(nickname).has_value(); }

bool ProfileBinding::setNickname(const QString& nickname) {
    auto value = canonical(nickname);
    if (!available_ || !value.has_value())
        return false;
    auto candidate = profile_;
    candidate.nickname = std::move(*value);
    return commit(std::move(candidate));
}

bool ProfileBinding::chooseBundledAvatar(const int index) {
    if (!available_ || index < 0 || index >= avatarCount())
        return false;
    auto candidate = profile_;
    candidate.avatar = {AvatarKind::Bundled, static_cast<std::uint32_t>(index), {}};
    return commit(std::move(candidate));
}

bool ProfileBinding::chooseCustomAvatar(const QUrl& file) {
    if (!available_ || !file.isLocalFile())
        return false;
    const auto png = normalizeAvatarImage(file.toLocalFile());
    if (!png.has_value())
        return false;
    const auto sha256 = saveCustomAvatar(config_directory_, *png);
    if (sha256.isEmpty())
        return false;
    auto candidate = profile_;
    candidate.avatar = {AvatarKind::Custom, 0, sha256.toStdString()};
    return commit(std::move(candidate));
}

bool ProfileBinding::commit(ListenerProfile candidate) {
    // During setup there is no nickname yet: the avatar choice waits for it in memory.
    if (needs_setup_ && candidate.nickname.empty()) {
        profile_ = std::move(candidate);
        emit changed();
        return true;
    }
    if (store_->save(candidate) != ProfileSaveStatus::Saved)
        return false;
    profile_ = std::move(candidate);
    needs_setup_ = false;
    emit changed();
    return true;
}
} // namespace music_player::m0
