#include "BrowserMediaSelection.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {
void BrowserMediaSelection::clearData() {
    snapshot_.reset();
    selected_.clear();
    confirmed_.clear();
}
void BrowserMediaSelection::navigationChanged(QString trusted_page_url) {
    pending_.reset();
    clearData();
    if (!browserMediaPageAllowed(trusted_page_url)) {
        page_url_.clear();
        state_ = BrowserMediaSelectionState::Unavailable;
        return;
    }
    page_url_ = std::move(trusted_page_url);
    state_ = BrowserMediaSelectionState::Idle;
}
std::optional<std::uint64_t> BrowserMediaSelection::begin(const Purpose purpose) {
    if (pending_ || page_url_.isEmpty() || serial_ == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    if (purpose == Purpose::Detection)
        clearData();
    else
        confirmed_.clear();
    pending_ = Pending{++serial_, purpose};
    state_ =
        purpose == Purpose::Detection ? BrowserMediaSelectionState::Detecting : BrowserMediaSelectionState::Confirming;
    return serial_;
}
std::optional<std::uint64_t> BrowserMediaSelection::beginDetection() {
    return canDetect() ? begin(Purpose::Detection) : std::nullopt;
}
std::optional<std::uint64_t> BrowserMediaSelection::beginConfirmation() {
    return canConfirm() ? begin(Purpose::Confirmation) : std::nullopt;
}
bool BrowserMediaSelection::finishFailure(const std::uint64_t operation, const BrowserMediaSelectionState state) {
    if (!pending_ || pending_->operation != operation)
        return false;
    pending_.reset();
    clearData();
    state_ = state;
    return true;
}
bool BrowserMediaSelection::complete(const std::uint64_t operation, const QByteArray& payload) {
    if (!pending_ || pending_->operation != operation)
        return false;
    try {
        auto fresh = decodeBrowserMediaSnapshot(payload);
        if (!fresh) {
            (void)finishFailure(operation, BrowserMediaSelectionState::InvalidSnapshot);
            return false;
        }
        if (fresh->page_url != page_url_) {
            (void)finishFailure(operation, BrowserMediaSelectionState::SourceChanged);
            return false;
        }
        if (pending_->purpose == Purpose::Detection) {
            snapshot_ = std::move(fresh);
            pending_.reset();
            state_ = BrowserMediaSelectionState::Ready;
            return true;
        }
        // A fresh scan may reorder/change other entries. Only the exact tuples
        // selected from the original list carry the user's current choice.
        std::vector<DownloadItem> confirmed;
        confirmed.reserve(selected_.size());
        for (const auto index : selected_) {
            const auto& original = snapshot_->candidates[index];
            if (std::ranges::find(fresh->candidates, original) == fresh->candidates.end()) {
                (void)finishFailure(operation, BrowserMediaSelectionState::SourceChanged);
                return false;
            }
            confirmed.push_back(original);
        }
        confirmed_.swap(confirmed);
        pending_.reset();
        state_ = BrowserMediaSelectionState::Confirmed;
        return true;
    } catch (...) {
        (void)finishFailure(operation, BrowserMediaSelectionState::Failed);
        return false;
    }
}
bool BrowserMediaSelection::fail(const std::uint64_t operation) {
    return finishFailure(operation, BrowserMediaSelectionState::Failed);
}
bool BrowserMediaSelection::timeout(const std::uint64_t operation) {
    return finishFailure(operation, BrowserMediaSelectionState::TimedOut);
}
bool BrowserMediaSelection::setSelected(const std::size_t index, const bool selected) {
    if (pending_ || (state_ != BrowserMediaSelectionState::Ready && state_ != BrowserMediaSelectionState::Confirmed) ||
        !snapshot_ || index >= snapshot_->candidates.size())
        return false;
    const auto position = std::ranges::lower_bound(selected_, index);
    if (position != selected_.end() && *position == index) {
        if (selected)
            return true;
        selected_.erase(position);
    } else {
        if (!selected)
            return true;
        if (selected_.size() >= 20)
            return false;
        selected_.insert(position, index);
    }
    confirmed_.clear();
    state_ = BrowserMediaSelectionState::Ready;
    return true;
}
bool BrowserMediaSelection::canDetect() const {
    return !pending_ && !page_url_.isEmpty() && serial_ != std::numeric_limits<std::uint64_t>::max();
}
bool BrowserMediaSelection::canConfirm() const {
    return canDetect() && snapshot_ && !selected_.empty() &&
           (state_ == BrowserMediaSelectionState::Ready || state_ == BrowserMediaSelectionState::Confirmed);
}
BrowserMediaSelectionState BrowserMediaSelection::state() const { return state_; }
const QString& BrowserMediaSelection::pageUrl() const { return page_url_; }
const std::optional<BrowserMediaSnapshot>& BrowserMediaSelection::snapshot() const { return snapshot_; }
const std::vector<std::size_t>& BrowserMediaSelection::selectedIndices() const { return selected_; }
std::vector<DownloadItem> BrowserMediaSelection::takeConfirmedItems() {
    if (pending_ || state_ != BrowserMediaSelectionState::Confirmed)
        return {};
    state_ = BrowserMediaSelectionState::Ready;
    return std::exchange(confirmed_, {});
}
} // namespace music_player::m0
