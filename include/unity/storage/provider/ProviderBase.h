#pragma once

#include <unity/storage/common/common.h>
#include <unity/storage/provider/visibility.h>

#include <boost/thread/future.hpp>

#include <sys/types.h>
#include <string>
#include <map>
#include <vector>

namespace unity
{
namespace storage
{
namespace provider
{

struct STORAGE_PROVIDER_EXPORT Item
{
    std::string item_id;
    std::string parent_id;
    std::string title;
    std::string etag;
    unity::storage::common::ItemType type;
    // Should be map<string,variant>
    std::map<std::string,std::string> metadata;
};

struct STORAGE_PROVIDER_EXPORT Context
{
    uid_t uid;
    pid_t pid;
    std::string security_label;
};

typedef std::vector<Item> ItemList;

class STORAGE_PROVIDER_EXPORT ProviderBase
{
public:
    ProviderBase();
    virtual ~ProviderBase();

    virtual boost::future<ItemList> roots(Context const& context) = 0;
    virtual boost::future<std::tuple<ItemList,std::string>> list(
        std::string const& item_id, std::string const& page_token,
        Context const& context) = 0;
    virtual boost::future<ItemList> lookup(
        std::string const& parent_id, std::string const& name,
        Context const& context) = 0;
    virtual boost::future<Item> metadata(std::string const& item_id,
        Context const& context) = 0;
};

}
}
}