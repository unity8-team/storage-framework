/*
 * Copyright (C) 2016 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: James Henstridge <james.henstridge@canonical.com>
 */

#include "TestProvider.h"
#include <unity/storage/provider/DownloadJob.h>
#include <unity/storage/provider/UploadJob.h>

#include <QTimer>
#include <unistd.h>
#include <algorithm>


using namespace std;
using namespace unity::storage;

class TestDownloadJob : public DownloadJob
{
public:
    TestDownloadJob(std::string const& download_id, std::string const& data);
    boost::future<void> cancel() override;
    boost::future<void> finish() override;
private:
    void write_some();

    std::string const data_;
    ssize_t bytes_written_ = 0;
    QTimer timer_;
};

TestDownloadJob::TestDownloadJob(std::string const& download_id,
                                 std::string const& data)
    : DownloadJob(download_id), data_(data)
{
    timer_.setSingleShot(false);
    timer_.setInterval(10);
    QObject::connect(&timer_, &QTimer::timeout, [this]() { write_some(); });
    timer_.start();
}

boost::future<void> TestDownloadJob::cancel()
{
    timer_.stop();
    boost::promise<void> p;
    p.set_value();
    return p.get_future();
}

boost::future<void> TestDownloadJob::finish()
{
    boost::promise<void> p;
    if (bytes_written_ < (ssize_t)data_.size())
    {
        p.set_exception(runtime_error("Not all data read"));
    }
    else
    {
        p.set_value();
    }
    return p.get_future();
}

void TestDownloadJob::write_some()
{
    if (bytes_written_ >= (ssize_t)data_.size()) {
        report_complete();
        timer_.stop();
        return;
    }

    ssize_t n_written = write(write_socket(), data_.data() + bytes_written_,
                              min(data_.size() - bytes_written_, (size_t)2));
    if (n_written < 0)
    {
        report_error(make_exception_ptr(runtime_error("Write failure")));
        timer_.stop();
    }
    else
    {
        bytes_written_ += n_written;
    }
}


boost::future<ItemList> TestProvider::roots(Context const& /*ctx*/)
{
    ItemList roots = {
        {"root_id", "", "Root", "etag", ItemType::root, {}},
    };
    boost::promise<ItemList> p;
    p.set_value(roots);
    return p.get_future();
}

boost::future<tuple<ItemList,string>> TestProvider::list(
    string const& item_id, string const& page_token, Context const& /*ctx*/)
{
    boost::promise<tuple<ItemList,string>> p;

    if (item_id != "root_id")
    {
        p.set_exception(runtime_error("Unknown folder"));
    }
    else if (page_token == "")
    {
        ItemList children = {
            {"child1_id", "root_id", "Child 1", "etag", ItemType::file, {}},
            {"child2_id", "root_id", "Child 2", "etag", ItemType::file, {}},
        };
        p.set_value(make_tuple(children, "page_token"));
    }
    else if (page_token == "page_token")
    {
        ItemList children = {
            {"child3_id", "root_id", "Child 4", "etag", ItemType::file, {}},
            {"child4_id", "root_id", "Child 3", "etag", ItemType::file, {}},
        };
        p.set_value(make_tuple(children, ""));
    }
    else
    {
        p.set_exception(runtime_error("Unknown page token"));
    }
    return p.get_future();
}

boost::future<ItemList> TestProvider::lookup(
    string const& parent_id, string const& name, Context const& /*ctx*/)
{
    boost::promise<ItemList> p;
    ItemList items = {
        {"child_id", parent_id, name, "etag", ItemType::file, {}},
    };
    p.set_value(items);
    return p.get_future();
}

boost::future<Item> TestProvider::metadata(
    string const& item_id, Context const& /*ctx*/)
{
    boost::promise<Item> p;
    if (item_id == "root_id")
    {
        Item item = {"root_id", "", "Root", "etag", ItemType::root, {}};
        p.set_value(item);
    }
    else
    {
        p.set_exception(runtime_error("Unknown item"));
    }
    return p.get_future();
}

boost::future<Item> TestProvider::create_folder(
    string const& parent_id, string const& name, Context const& /*ctx*/)
{
    boost::promise<Item> p;
    Item item = {"new_folder_id", parent_id, name, "etag", ItemType::folder, {}};
    p.set_value(item);
    return p.get_future();
}

boost::future<unique_ptr<UploadJob>> TestProvider::create_file(
    string const& parent_id, string const& name,
    int64_t size, string const& content_type, bool allow_overwrite,
    Context const& ctx)
{
    boost::promise<unique_ptr<UploadJob>> p;
    p.set_value(unique_ptr<UploadJob>());
    return p.get_future();
}

boost::future<unique_ptr<UploadJob>> TestProvider::update(
    string const& item_id, int64_t size, string const& old_etag,
    Context const& ctx)
{
    boost::promise<unique_ptr<UploadJob>> p;
    p.set_value(unique_ptr<UploadJob>());
    return p.get_future();
}

boost::future<unique_ptr<DownloadJob>> TestProvider::download(
    string const& /*item_id*/, Context const& /*ctx*/)
{
    boost::promise<unique_ptr<DownloadJob>> p;
    p.set_value(unique_ptr<DownloadJob>(
                    new TestDownloadJob("download_id", "Hello world")));
    return p.get_future();
}

boost::future<void> TestProvider::delete_item(
    string const& item_id, Context const& /*ctx*/)
{
    boost::promise<void> p;
    if (item_id == "item_id")
    {
        p.set_value();
    }
    else
    {
        p.set_exception(runtime_error("Bad filename"));
    }
    return p.get_future();
}

boost::future<Item> TestProvider::move(
    string const& item_id, string const& new_parent_id,
    string const& new_name, Context const& /*ctx*/)
{
    boost::promise<Item> p;
    Item item = {item_id, new_parent_id, new_name, "etag", ItemType::file, {}};
    p.set_value(item);
    return p.get_future();
}

boost::future<Item> TestProvider::copy(
    string const& item_id, string const& new_parent_id,
    string const& new_name, Context const& /*ctx*/)
{
    boost::promise<Item> p;
    Item item = {"new_id", new_parent_id, new_name, "etag", ItemType::file, {}};
    p.set_value(item);
    return p.get_future();
}
