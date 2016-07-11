#include <unity/storage/qt/client/internal/remote_client/RootImpl.h>

#include "ProviderInterface.h"
#include <unity/storage/qt/client/internal/make_future.h>
#include <unity/storage/qt/client/internal/remote_client/FileImpl.h>
#include <unity/storage/qt/client/internal/remote_client/Handler.h>

using namespace std;

namespace unity
{
namespace storage
{
namespace qt
{
namespace client
{
namespace internal
{
namespace remote_client
{

RootImpl::RootImpl(storage::internal::ItemMetadata const& md, weak_ptr<Account> const& account)
    : ItemBase(md.item_id, ItemType::root)
    , FolderBase(md.item_id, ItemType::root)
    , RootBase(md.item_id, account)
    , ItemImpl(md, ItemType::root)
    , FolderImpl(md, ItemType::root)
{
}

QFuture<QVector<Folder::SPtr>> RootImpl::parents() const
{
    return make_ready_future(QVector<Folder::SPtr>());  // For the root, we return an empty vector.
}

QVector<QString> RootImpl::parent_ids() const
{
    return QVector<QString>();  // For the root, we return an empty vector.
}

QFuture<void> RootImpl::delete_item()
{
    return make_exceptional_future(StorageException());  // Cannot delete root.
}

QFuture<int64_t> RootImpl::free_space_bytes() const
{
    // TODO, need to refresh metadata here instead.
    return make_ready_future(int64_t(1));
}

QFuture<int64_t> RootImpl::used_space_bytes() const
{
    // TODO, need to refresh metadata here instead.
    return make_ready_future(int64_t(1));
}

QFuture<Item::SPtr> RootImpl::get(QString native_identity) const
{
    auto reply = provider().Metadata(native_identity);

    auto process_reply = [this](decltype(reply) const& reply, QFutureInterface<Item::SPtr>& qf)
    {
        auto md = reply.value();
        Item::SPtr item;
        if (md.type == ItemType::root)
        {
            item = make_root(md, dynamic_pointer_cast<RootImpl>(root_.lock()->p_)->account_);
        }
        else
        {
            item = ItemImpl::make_item(md, root_);
        }
        make_ready_future(qf, item);
    };

    auto handler = new Handler<Item::SPtr>(const_cast<RootImpl*>(this), reply, process_reply);
    return handler->future();
}

Root::SPtr RootImpl::make_root(storage::internal::ItemMetadata const& md, weak_ptr<Account> const& account)
{
    assert(md.type == ItemType::root);
    assert(account.lock());

    auto impl = new RootImpl(md, account);
    Root::SPtr root(new Root(impl));
    impl->set_root(root);
    impl->set_public_instance(root);
    return root;
}

}  // namespace remote_client
}  // namespace internal
}  // namespace client
}  // namespace qt
}  // namespace storage
}  // namespace unity
