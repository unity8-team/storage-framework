#pragma once

#include <unity/storage/qt/client/Folder.h>

namespace unity
{
namespace storage
{
namespace qt
{
namespace client
{

class Account;
class Item;

namespace internal
{

class AccountImpl;
class RootImpl;

}

/**
\brief Class that represents a root folder.
*/
class Root : public Folder
{
public:
    ~Root();
    Root(Root const&) = delete;
    Root& operator=(Root const&) = delete;
    Root(Root&&);
    Root& operator=(Root&&);

    typedef std::shared_ptr<Root> SPtr;

    /**
    \brief Returns the account for this item.
    */
    Account* account() const;

    QFuture<int64_t> free_space_bytes() const;
    QFuture<int64_t> used_space_bytes() const;

    QFuture<Item::SPtr> get(QString native_identity) const;

    // TODO: Do we need a method to get lots of things?

private:
    Root(internal::RootImpl*);

    std::unique_ptr<internal::RootImpl> p_;

    friend class internal::AccountImpl;
};

}  // namespace client
}  // namespace qt
}  // namespace storage
}  // namespace unity