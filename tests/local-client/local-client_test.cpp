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
 * Authors: Michi Henning <michi.henning@canonical.com>
 */

#include <unity/storage/qt/client/client-api.h>

#include <unity/storage/qt/client/internal/boost_filesystem.h>
#include <unity/storage/qt/client/internal/local_client/tmpfile-prefix.h>

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QTimer>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <glib.h>
#pragma GCC diagnostic pop

#include <fstream>

using namespace unity::storage;
using namespace unity::storage::qt::client;
using namespace std;

static constexpr int SIGNAL_WAIT_TIME = 1000;

// Bunch of helper function to reduce the amount of noise in the tests.

Account::SPtr get_account(Runtime::SPtr const& runtime)
{
    auto accounts = runtime->accounts().result();
    assert(accounts.size() == 1);
    return accounts[0];
}

Root::SPtr get_root(Runtime::SPtr const& runtime)
{
    auto acc = get_account(runtime);
    auto roots = acc->roots().result();
    assert(roots.size() == 1);
    return roots[0];
}

Folder::SPtr get_parent(Item::SPtr const& item)
{
    assert(item->type() != ItemType::root);
    auto parents = item->parents().result();
    return parents[0];
}

void clear_folder(Folder::SPtr folder)
{
    auto items = folder->list().result();
    for (auto i : items)
    {
        i->delete_item().waitForFinished();
    }
}

bool content_matches(File::SPtr const& file, QByteArray const& expected)
{
    QFile f(file->native_identity());
    assert(f.open(QIODevice::ReadOnly));
    QByteArray buf = f.readAll();
    return buf == expected;
}

void write_file(Folder::SPtr const& folder, QString const& name, QByteArray const& contents)
{
    QString ofile = folder->native_identity() + "/" + name;
    QFile f(ofile);
    assert(f.open(QIODevice::Truncate | QIODevice::WriteOnly));
    if (!contents.isEmpty())
    {
        assert(f.write(contents));
    }
}

TEST(Runtime, lifecycle)
{
    auto runtime = Runtime::create();
    runtime->shutdown();
    runtime->shutdown();  // Just to show that this is safe.
}

TEST(Runtime, basic)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    EXPECT_EQ(runtime, acc->runtime());
    auto owner = acc->owner();
    EXPECT_EQ(QString(g_get_user_name()), owner);
    auto owner_id = acc->owner_id();
    EXPECT_EQ(QString::number(getuid()), owner_id);
    auto description = acc->description();
    EXPECT_EQ(description, QString("Account for ") + owner + " (" + owner_id + ")");
}

TEST(Runtime, accounts)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto roots = acc->roots().result();
    EXPECT_EQ(1, roots.size());

    // Get roots again, to get coverage for lazy initialization.
    roots = acc->roots().result();
    ASSERT_EQ(1, roots.size());
}

TEST(Root, basic)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    EXPECT_EQ(acc, root->account());
    EXPECT_EQ(ItemType::root, root->type());
    EXPECT_EQ("", root->name());
    EXPECT_NE("", root->etag());

    auto parents = root->parents().result();
    EXPECT_TRUE(parents.isEmpty());
    EXPECT_TRUE(root->parent_ids().isEmpty());

    // get(<root-path>) must return the root.
    auto item = root->get(root->native_identity()).result();
    EXPECT_NE(nullptr, dynamic_pointer_cast<Root>(item));
    EXPECT_TRUE(root->equal_to(item));

    // Free and used space can be anything, but must be > 0.
    auto free_space = root->free_space_bytes().result();
    cerr << "bytes free: " << free_space << endl;
    EXPECT_GT(free_space, 0);

    auto used_space = root->used_space_bytes().result();
    cerr << "bytes used: " << used_space << endl;
    EXPECT_GT(used_space, 0);
}

TEST(Folder, basic)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    auto items = root->list().result();
    EXPECT_TRUE(items.isEmpty());

    // Create a file and check that it was created with correct type, name, and size 0.
    auto uploader = root->create_file("file1", 0).result();
    auto file = uploader->finish_upload().result();
    EXPECT_EQ(ItemType::file, file->type());
    EXPECT_EQ("file1", file->name());
    EXPECT_EQ(0, file->size());
    EXPECT_EQ(root->native_identity() + "/file1", file->native_identity());

    // Create a folder and check that it was created with correct type and name.
    auto folder = root->create_folder("folder1").result();
    EXPECT_EQ(ItemType::folder, folder->type());
    EXPECT_EQ("folder1", folder->name());
    EXPECT_EQ(root->native_identity() + "/folder1", folder->native_identity());

    // Check that we can find both file1 and folder1.
    auto item = root->lookup("file1").result()[0];
    file = dynamic_pointer_cast<File>(item);
    ASSERT_NE(nullptr, file);
    EXPECT_EQ("file1", file->name());
    EXPECT_EQ(0, file->size());

    item = root->lookup("folder1").result()[0];
    folder = dynamic_pointer_cast<Folder>(item);
    ASSERT_NE(nullptr, folder);
    ASSERT_EQ(nullptr, dynamic_pointer_cast<Root>(folder));
    EXPECT_EQ("folder1", folder->name());

    item = root->get(file->native_identity()).result();
    file = dynamic_pointer_cast<File>(item);
    ASSERT_NE(nullptr, file);
    EXPECT_EQ("file1", file->name());
    EXPECT_EQ(0, file->size());

    item = root->get(folder->native_identity()).result();
    folder = dynamic_pointer_cast<Folder>(item);
    ASSERT_NE(nullptr, folder);
    ASSERT_EQ(nullptr, dynamic_pointer_cast<Root>(folder));
    EXPECT_EQ("folder1", folder->name());

    // Check that list() returns file1 and folder1.
    items = root->list();
    ASSERT_EQ(2, items.size());
    auto left = items[0];
    auto right = items[1];
    ASSERT_TRUE((dynamic_pointer_cast<File>(left) && dynamic_pointer_cast<Folder>(right))
                ||
                (dynamic_pointer_cast<File>(right) && dynamic_pointer_cast<Folder>(left)));
    if (dynamic_pointer_cast<File>(left))
    {
        file = dynamic_pointer_cast<File>(left);
        folder = dynamic_pointer_cast<Folder>(right);
    }
    else
    {
        file = dynamic_pointer_cast<File>(right);
        folder = dynamic_pointer_cast<Folder>(left);
    }
    EXPECT_EQ("file1", file->name());
    EXPECT_EQ("folder1", folder->name());
    EXPECT_TRUE(file->root()->equal_to(root));
    EXPECT_TRUE(folder->root()->equal_to(root));

    // Parent of both file and folder must be the root.
    EXPECT_TRUE(root->equal_to(get_parent(file)));
    EXPECT_TRUE(root->equal_to(get_parent(folder)));
    EXPECT_EQ(root->native_identity(), file->parent_ids()[0]);
    EXPECT_EQ(root->native_identity(), folder->parent_ids()[0]);

    // Delete the file and check that only the directory is left.
    file->delete_item().waitForFinished();
    items = root->list().result();
    ASSERT_EQ(1, items.size());
    folder = dynamic_pointer_cast<Folder>(items[0]);
    ASSERT_NE(nullptr, folder);
    EXPECT_EQ("folder1", folder->name());;

    // Delete the folder and check that the root is empty.
    folder->delete_item().waitForFinished();
    items = root->list().result();
    ASSERT_EQ(0, items.size());
}

TEST(Folder, nested)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    auto d1 = root->create_folder("d1").result();
    auto d2 = d1->create_folder("d2").result();

    // Parent of d2 must be d1.
    EXPECT_TRUE(get_parent(d2)->equal_to(d1));
    EXPECT_TRUE(d2->parent_ids()[0] == d1->native_identity());

    // Delete is recursive
    d1->delete_item().waitForFinished();
    auto items = root->list().result();
    ASSERT_EQ(0, items.size());
}

template<typename T>
bool wait(T fut)
{
    QFutureWatcher<decltype(fut.result())> w;
    QSignalSpy spy(&w, &decltype(w)::finished);
    w.setFuture(fut);
    return spy.wait(SIGNAL_WAIT_TIME);
}

template<>
bool wait(QFuture<void> fut)
{
    QFutureWatcher<void> w;
    QSignalSpy spy(&w, &decltype(w)::finished);
    w.setFuture(fut);
    return spy.wait(SIGNAL_WAIT_TIME);
}

TEST(File, upload)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    {
        // Upload a few bytes.
        QByteArray const contents = "Hello\n";
        auto uploader = root->create_file("new_file", contents.size()).result();
        auto written = uploader->socket()->write(contents);
        ASSERT_EQ(contents.size(), written);

        auto file_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(file_fut));
        auto file = file_fut.result();
        EXPECT_EQ(contents.size(), file->size());
        ASSERT_TRUE(content_matches(file, contents));

        // Calling finish_upload() more than once must return the original future.
        auto file2 = uploader->finish_upload().result();
        EXPECT_TRUE(file2->equal_to(file));

        // Calling cancel() after finish_upload must do nothing.
        uploader->cancel();
        file2 = uploader->finish_upload().result();
        EXPECT_TRUE(file2->equal_to(file));

        file->delete_item().waitForFinished();
    }

    {
        // Upload exactly 64 KB.
        QByteArray const contents(64 * 1024, 'a');
        auto uploader = root->create_file("new_file", contents.size()).result();
        auto written = uploader->socket()->write(contents);
        ASSERT_EQ(contents.size(), written);

        auto file_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(file_fut));
        auto file = file_fut.result();
        EXPECT_EQ(contents.size(), file->size());
        ASSERT_TRUE(content_matches(file, contents));

        file->delete_item().waitForFinished();
    }

    {
        // Upload 1000 KBj
        QByteArray const contents(1000 * 1024, 'a');
        auto uploader = root->create_file("new_file", contents.size()).result();
        auto written = uploader->socket()->write(contents);
        ASSERT_EQ(contents.size(), written);

        auto file_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(file_fut));
        auto file = file_fut.result();
        EXPECT_EQ(contents.size(), file->size());
        ASSERT_TRUE(content_matches(file, contents));

        file->delete_item().waitForFinished();
    }

    {
        // Upload empty file.
        auto uploader = root->create_file("new_file", 0).result();
        auto file = uploader->finish_upload().result();
        ASSERT_EQ(0, file->size());

        // Again, and check that the ETag is different.
        auto old_etag = file->etag();
        sleep(1);
        uploader = file->create_uploader(ConflictPolicy::overwrite, 0).result();
        file = uploader->finish_upload().result();
        EXPECT_NE(old_etag, file->etag());

        file->delete_item().waitForFinished();
    }

    {
        // Let the uploader go out of scope and check that the file was not created.
        root->create_file("new_file", 0).result();
        boost::filesystem::path path(TEST_DIR "/storage-framework/new_file");
        auto status = boost::filesystem::status(path);
        ASSERT_FALSE(boost::filesystem::exists(status));
    }
}

TEST(File, create_uploader)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    // Make a new file first.
    auto uploader = root->create_file("new_file", 0).result();
    auto file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    auto file = file_fut.result();
    EXPECT_EQ(0, file->size());
    auto old_etag = file->etag();

    // Create uploader for the file and write nothing.
    uploader = file->create_uploader(ConflictPolicy::overwrite, 0).result();
    file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    file = file_fut.result();
    EXPECT_EQ(0, file->size());

    // Same test again, but this time, we write a bunch of data.
    std::string s(1000000, 'a');
    uploader = file->create_uploader(ConflictPolicy::overwrite, s.size()).result();
    uploader->socket()->write(&s[0], s.size());

    // Need to sleep here, otherwise it is possible for the
    // upload to finish within the granularity of the file system time stamps.
    sleep(1);
    file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    file = file_fut.result();
    EXPECT_EQ(1000000, file->size());
    EXPECT_NE(old_etag, file->etag());

    file->delete_item().waitForFinished();
}

TEST(File, cancel_upload)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    {
        auto uploader = root->create_file("new_file", 20).result();

        // We haven't called finish_upload(), so the cancel is guaranteed
        // to catch the uploader in the in_progress state.
        uploader->cancel();
        EXPECT_THROW(uploader->finish_upload().result(), CancelledException);

        boost::filesystem::path path(TEST_DIR "/storage-framework/new_file");
        auto status = boost::filesystem::status(path);
        ASSERT_FALSE(boost::filesystem::exists(status));
    }

    {
        // Create a file with a few bytes.
        QByteArray original_contents = "Hello World!\n";
        write_file(root, "new_file", original_contents);
        auto file = dynamic_pointer_cast<File>(root->lookup("new_file").result()[0]);
        ASSERT_NE(nullptr, file);

        // Create an uploader for the file and write a bunch of bytes.
        auto uploader = file->create_uploader(ConflictPolicy::overwrite, original_contents.size()).result();
        QByteArray const contents(1024 * 1024, 'a');
        auto written = uploader->socket()->write(contents);
        ASSERT_EQ(contents.size(), written);

        // No finish_upload() here, so the transfer is still in progress. Now cancel.
        auto cancel_fut = uploader->cancel();
        ASSERT_TRUE(wait(cancel_fut));

        // finish_upload() must indicate that the upload was cancelled.
        EXPECT_THROW(uploader->finish_upload().result(), CancelledException);

        // The original file contents must still be intact.
        EXPECT_EQ(original_contents.size(), file->size());
        ASSERT_TRUE(content_matches(file, original_contents));

        file->delete_item().waitForFinished();
    }
}

TEST(File, upload_conflict)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    // Make a new file on disk.
    QByteArray const contents = "";
    write_file(root, "new_file", contents);
    auto file = dynamic_pointer_cast<File>(root->lookup("new_file").result()[0]);
    ASSERT_NE(nullptr, file);
    auto uploader = file->create_uploader(ConflictPolicy::error_if_conflict, contents.size()).result();

    // Touch the file on disk to give it a new time stamp.
    sleep(1);
    ASSERT_EQ(0, system((string("touch ") + file->native_identity().toStdString()).c_str()));

    try
    {
        // Must get an exception because the time stamps no longer match.
        uploader->finish_upload().result();
        FAIL();
    }
    catch (ConflictException const&)
    {
        // TODO: check exception details.
    }

    file->delete_item().waitForFinished();
}

TEST(File, download)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    {
        // Download a few bytes.
        QByteArray const contents = "Hello\n";
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        auto socket = downloader->socket();
        QByteArray buf;
        do
        {
            // Need to pump the event loop while the socket does its thing.
            QSignalSpy spy(downloader->socket().get(), &QIODevice::readyRead);
            ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));
            auto bytes_to_read = socket->bytesAvailable();
            buf.append(socket->read(bytes_to_read));
        } while (buf.size() < contents.size());

        // Wait for disconnected signal.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());

        // Contents must match.
        EXPECT_EQ(contents, buf);
    }

    {
        // Download exactly 64 KB.
        QByteArray const contents(64 * 1024, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        auto socket = downloader->socket();
        QByteArray buf;
        do
        {
            // Need to pump the event loop while the socket does its thing.
            QSignalSpy spy(downloader->socket().get(), &QIODevice::readyRead);
            ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));
            auto bytes_to_read = socket->bytesAvailable();
            buf.append(socket->read(bytes_to_read));
        } while (buf.size() < contents.size());

        // Wait for disconnected signal.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());

        // Contents must match
        EXPECT_EQ(contents, buf);
    }

    {
        // Download 1 MB + 1 bytes.
        QByteArray const contents(1024 * 1024 + 1, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        auto socket = downloader->socket();
        QByteArray buf;
        do
        {
            // Need to pump the event loop while the socket does its thing.
            QSignalSpy spy(downloader->socket().get(), &QIODevice::readyRead);
            ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));
            auto bytes_to_read = socket->bytesAvailable();
            buf.append(socket->read(bytes_to_read));
        } while (buf.size() < contents.size());

        // Wait for disconnected signal.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());

        // Contents must match
        EXPECT_EQ(contents, buf);
    }

    {
        // Download file containing zero bytes
        QByteArray const contents;
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        auto socket = downloader->socket();

        // No readyRead every arrives in this case, just wait for disconnected.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());
    }

    {
        // Don't ever call read on empty file.
        QByteArray const contents;
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        // No readyRead ever arrives in this case, just wait for disconnected.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        // This succeeds because the provider disconnects as soon
        // as it realizes that there is nothing to write.
        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());
    }

    {
        // Don't ever call read on small file.
        QByteArray const contents("some contents");
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        // Wait for disconnected.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        // This succeeds because the provider has written everything and disconnected.
        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());
    }

    {
        // Don't ever call read on large file.
        QByteArray const contents(1024 * 1024, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
        EXPECT_TRUE(file->equal_to(downloader->file()));

        // Wait for first readyRead. Not all data fits into the socket buffer.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::readyRead);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        // This fails because the provider still has data left to write.
        try
        {
            downloader->finish_download().waitForFinished();
            FAIL();
        }
        catch (StorageException const& e)
        {
            // TODO: check exception details
        }
    }

    {
        // Let downloader go out of scope.
        QByteArray const contents(1024 * 1024, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader = file->create_downloader().result();
    }

    {
        // Let downloader future go out of scope.
        QByteArray const contents(1024 * 1024, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto downloader_fut = file->create_downloader();
    }
}

TEST(File, cancel_download)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    {
        // Download enough bytes to prevent a single write in the provider from completing the download.
        QByteArray const contents(1024 * 1024, 'a');
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        auto download_fut = file->create_downloader();
        ASSERT_TRUE(wait(download_fut));
        auto downloader = download_fut.result();

        // We haven't read anything, so the cancel is guaranteed to catch the
        // downloader in the in_progress state.
        auto cancel_fut = downloader->cancel();
        ASSERT_TRUE(wait(cancel_fut));
        ASSERT_THROW(downloader->finish_download().waitForFinished(), CancelledException);
    }

    {
        // Download a few bytes.
        QByteArray const contents = "Hello\n";
        write_file(root, "file", contents);

        auto item = root->lookup("file").result()[0];
        File::SPtr file = dynamic_pointer_cast<File>(item);
        ASSERT_FALSE(file == nullptr);

        // Finish the download.
        auto downloader = file->create_downloader().result();
        auto socket = downloader->socket();
        QByteArray buf;
        do
        {
            // Need to pump the event loop while the socket does its thing.
            QSignalSpy spy(downloader->socket().get(), &QIODevice::readyRead);
            ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));
            auto bytes_to_read = socket->bytesAvailable();
            buf.append(socket->read(bytes_to_read));
        } while (buf.size() < contents.size());

        // Wait for disconnected signal.
        QSignalSpy spy(downloader->socket().get(), &QLocalSocket::disconnected);
        ASSERT_TRUE(spy.wait(SIGNAL_WAIT_TIME));

        // Now send the cancel. The download is finished already, and the cancel
        // is too late, so finish_download() must report that the download
        // worked OK.
        downloader->cancel();
        ASSERT_NO_THROW(downloader->finish_download().waitForFinished());
    }
}

TEST(Item, move)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    // Check that rename works within the same folder.
    QByteArray const contents = "Hello\n";
    write_file(root, "f1", contents);
    auto f1 = root->lookup("f1").result()[0];
    auto f2 = f1->move(root, "f2").result();
    EXPECT_EQ("f2", f2->name());
    EXPECT_THROW(f1->name(), DeletedException);  // TODO: check exception details.

    // File must be found under new name.
    auto items = root->list().result();
    ASSERT_EQ(1, items.size());
    f2 = dynamic_pointer_cast<File>(items[0]);
    ASSERT_FALSE(f2 == nullptr);

    // Make a folder and move f2 into it.
    auto folder = root->create_folder("folder").result();
    f2 = f2->move(folder, "f2").result();
    EXPECT_TRUE(get_parent(f2)->equal_to(folder));

    // Move the folder
    auto item = folder->move(root, "folder2").result();
    folder = dynamic_pointer_cast<Folder>(item);
    EXPECT_EQ("folder2", folder->name());
}

TEST(Item, copy)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    QByteArray const contents = "hello\n";
    write_file(root, "file", contents);

    auto item = root->lookup("file").result()[0];
    auto copied_item = item->copy(root, "copy_of_file").result();
    EXPECT_EQ("copy_of_file", copied_item->name());
    File::SPtr copied_file = dynamic_pointer_cast<File>(item);
    ASSERT_NE(nullptr, copied_file);
    EXPECT_TRUE(content_matches(copied_file, contents));
}

TEST(Item, recursive_copy)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    // Create the following structure:
    // folder
    // folder/empty_folder
    // folder/non_empty_folder
    // folder/non_empty_folder/nested_file
    // folder/file

    string root_path = root->native_identity().toStdString();
    ASSERT_EQ(0, mkdir((root_path + "/folder").c_str(), 0700));
    ASSERT_EQ(0, mkdir((root_path + "/folder/empty_folder").c_str(), 0700));
    ASSERT_EQ(0, mkdir((root_path + "/folder/non_empty_folder").c_str(), 0700));
    ofstream(root_path + "/folder/non_empty_folder/nested_file");
    ofstream(root_path + "/folder/file");

    // Copy folder to folder2
    auto folder = dynamic_pointer_cast<Folder>(root->lookup("folder").result()[0]);
    ASSERT_NE(nullptr, folder);
    auto item = folder->copy(root, "folder2").result();

    // Verify that folder2 now contains the same structure as folder.
    auto folder2 = dynamic_pointer_cast<Folder>(item);
    ASSERT_NE(nullptr, folder2);
    EXPECT_NO_THROW(folder2->lookup("empty_folder").result()[0]);
    item = folder2->lookup("non_empty_folder").result()[0];
    auto non_empty_folder = dynamic_pointer_cast<Folder>(item);
    ASSERT_NE(nullptr, non_empty_folder);
    EXPECT_NO_THROW(non_empty_folder->lookup("nested_file").result()[0]);
    EXPECT_NO_THROW(folder2->lookup("file").result()[0]);
}

TEST(Item, modified_time)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    auto now = QDateTime::currentDateTimeUtc();
    sleep(1);
    auto uploader = root->create_file("file", 0).result();
    auto file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    auto file = file_fut.result();
    auto t = file->last_modified_time();
    // Rough check that the time is sane.
    EXPECT_LE(now, t);
    EXPECT_LE(t, now.addSecs(5));
}

TEST(Item, comparison)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    // Create two files.
    auto uploader = root->create_file("file1", 0).result();
    auto file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    auto file1 = file_fut.result();

    uploader = root->create_file("file2", 0).result();
    file_fut = uploader->finish_upload();
    ASSERT_TRUE(wait(file_fut));
    auto file2 = file_fut.result();

    EXPECT_FALSE(file1->equal_to(file2));

    // Retrieve file1 via lookup, so we get a different proxy.
    auto item = root->lookup("file1").result()[0];
    auto other_file1 = dynamic_pointer_cast<File>(item);
    EXPECT_NE(file1, other_file1);              // Compares shared_ptr values
    EXPECT_TRUE(file1->equal_to(other_file1));  // Deep comparison

    // Comparing against a deleted file must return false.
    auto delete_fut = file1->delete_item();
    ASSERT_TRUE(wait(delete_fut));
    EXPECT_FALSE(file1->equal_to(file2));
    EXPECT_FALSE(file2->equal_to(file1));

    // Delete file2 as well and compare again.
    delete_fut = file2->delete_item();
    ASSERT_TRUE(wait(delete_fut));
    EXPECT_FALSE(file1->equal_to(file2));
}

TEST(Root, root_exceptions)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    {
        auto fut = root->delete_item();
        try
        {
            fut.waitForFinished();
            FAIL();
        }
        catch (LogicException const& e)
        {
            EXPECT_EQ("Root::delete_item(): Cannot delete root folder", e.error_message());
        }
    }

    {
        auto fut = root->get("abc");
        try
        {
            fut.waitForFinished();
            FAIL();
        }
        catch (InvalidArgumentException const& e)
        {
            EXPECT_EQ("Root::get(): identity \"abc\" must be an absolute path", e.error_message());
        }
    }

    {
        auto fut = root->get("/etc");
        try
        {
            fut.waitForFinished();
            FAIL();
        }
        catch (InvalidArgumentException const& e)
        {
            EXPECT_EQ("Root::get(): identity \"/etc\" points outside the root folder", e.error_message());
        }
    }

    {
        auto folder = root->create_folder("folder").result();
        write_file(root, "folder/testfile", "hello");

        auto file = dynamic_pointer_cast<File>(folder->lookup("testfile").result()[0]);
        ASSERT_NE(nullptr, file);

        // Remove permission from folder.
        string cmd = "chmod -x " + folder->native_identity().toStdString();
        ASSERT_EQ(0, system(cmd.c_str()));

        try
        {
            file = dynamic_pointer_cast<File>(root->get(file->native_identity()).result());
            FAIL();
        }
        catch (PermissionException const& e)
        {
            EXPECT_TRUE(e.error_message().startsWith("Root::get(): "));
            EXPECT_TRUE(e.error_message().contains("Permission denied"));
        }

        cmd = "chmod +x " + folder->native_identity().toStdString();
        ASSERT_EQ(0, system(cmd.c_str()));

        clear_folder(root);
    }

    {
        write_file(root, "testfile", "hello");

        auto file = dynamic_pointer_cast<File>(root->lookup("testfile").result()[0]);
        ASSERT_NE(nullptr, file);

        QString id = file->native_identity();
        id.append("_doesnt_exist");

        try
        {
            file = dynamic_pointer_cast<File>(root->get(id).result());
            FAIL();
        }
        catch (NotExistsException const& e)
        {
            EXPECT_EQ(id, e.key());
        }

        clear_folder(root);
    }

    {
        auto fifo_id = root->native_identity() + "/fifo";
        string cmd = "mkfifo " + fifo_id.toStdString();
        ASSERT_EQ(0, system(cmd.c_str()));

        try
        {
            root->get(fifo_id).result();
            FAIL();
        }
        catch (NotExistsException const& e)
        {
            EXPECT_EQ(fifo_id, e.key());
        }

        cmd = "rm " + fifo_id.toStdString();
        system(cmd.c_str());
    }

    {
        QString reserved_name = TMPFILE_PREFIX "somefile";
        write_file(root, reserved_name, "some bytes");

        auto reserved_id = QString(TEST_DIR) + "/storage-framework/" + reserved_name;
        try
        {
            root->get(reserved_id).result();
            FAIL();
        }
        catch (NotExistsException const& e)
        {
            EXPECT_EQ(reserved_id, e.key());
        }

        clear_folder(root);
    }
}

File::SPtr make_deleted_file(Folder::SPtr parent, QString const& name)
{
    write_file(parent, name, "bytes");
    auto file_fut = parent->lookup("file");
    assert(wait(file_fut));
    auto file = dynamic_pointer_cast<File>(file_fut.result()[0]);
    auto void_fut = file->delete_item();
    assert(wait(void_fut));
    return file;
}

Folder::SPtr make_deleted_folder(Folder::SPtr parent, QString const& name)
{
    auto fut = parent->create_folder(name);
    assert(wait(fut));
    auto folder = fut.result();
    auto void_fut = folder->delete_item();
    assert(wait(void_fut));
    return folder;
}

TEST(Item, deleted_exceptions)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    try
    {
        auto file = make_deleted_file(root, "file");
        file->etag();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::etag(): "));
        EXPECT_TRUE(e.error_message().endsWith(" was deleted previously"));
        EXPECT_EQ(TEST_DIR "/storage-framework/file", e.native_identity());
    }

    try
    {
        auto file = make_deleted_file(root, "file");
        file->metadata();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::metadata(): "));
    }

    try
    {
        auto file = make_deleted_file(root, "file");
        file->last_modified_time();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::last_modified_time(): "));
    }

    try
    {
        // Copying deleted file must fail.
        auto file = make_deleted_file(root, "file");
        auto fut = file->copy(root, "copy_of_file");
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::copy(): "));
    }

    try
    {
        // Copying file into deleted folder must fail.

        // Make target folder.
        auto fut = root->create_folder("folder");
        ASSERT_TRUE(wait(fut));
        auto folder = fut.result();

        // Make a file in the root.
        auto uploader_fut = root->create_file("file", 0);
        ASSERT_TRUE(wait(uploader_fut));
        auto uploader = uploader_fut.result();
        auto finish_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(finish_fut));
        auto file = finish_fut.result();

        // Delete folder.
        auto void_fut = folder->delete_item();
        ASSERT_TRUE(wait(void_fut));

        auto copy_fut = file->copy(folder, "file");
        ASSERT_TRUE(wait(copy_fut));
        copy_fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::copy(): "));
    }
    clear_folder(root);

    try
    {
        // Moving deleted file must fail.
        auto file = make_deleted_file(root, "file");
        auto fut = file->move(root, "moved_file");
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::move(): "));
    }

    try
    {
        // Moving file into deleted folder must fail.

        // Make target folder.
        auto fut = root->create_folder("folder");
        ASSERT_TRUE(wait(fut));
        auto folder = fut.result();

        // Make a file in the root.
        auto uploader_fut = root->create_file("file", 0);
        ASSERT_TRUE(wait(uploader_fut));
        auto uploader = uploader_fut.result();
        auto finish_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(finish_fut));
        auto file = finish_fut.result();

        // Delete folder.
        auto void_fut = folder->delete_item();
        ASSERT_TRUE(wait(void_fut));

        auto move_fut = file->move(folder, "file");
        ASSERT_TRUE(wait(move_fut));
        move_fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::move(): "));
    }
    clear_folder(root);

    try
    {
        auto file = make_deleted_file(root, "file");
        file->parents().result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::parents(): "));
    }

    try
    {
        auto file = make_deleted_file(root, "file");
        file->parent_ids();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::parent_ids(): "));
    }

    try
    {
        // Deleting a deleted item must fail.
        auto file = make_deleted_file(root, "file");
        auto delete_fut = file->delete_item();
        ASSERT_TRUE(wait(delete_fut));
        delete_fut.waitForFinished();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("file", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Item::delete_item(): "));
    }
}

TEST(Folder, deleted_exceptions)
{
    auto runtime = Runtime::create();

    auto acc = get_account(runtime);
    auto root = get_root(runtime);
    clear_folder(root);

    try
    {
        auto folder = make_deleted_folder(root, "folder");
        folder->name();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Folder::name(): "));
    }

    try
    {
        auto folder = make_deleted_folder(root, "folder");
        auto fut = folder->list();
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Folder::list(): "));
    }

    try
    {
        auto folder = make_deleted_folder(root, "folder");
        auto fut = folder->lookup("something");
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Folder::lookup(): "));
    }

    try
    {
        auto folder = make_deleted_folder(root, "folder");
        auto fut = folder->create_folder("nested_folder");
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Folder::create_folder(): "));
    }

    try
    {
        auto folder = make_deleted_folder(root, "folder");
        auto fut = folder->create_file("nested_file", 0);
        ASSERT_TRUE(wait(fut));
        fut.result();
        FAIL();
    }
    catch (DeletedException const& e)
    {
        EXPECT_EQ("folder", e.name());
        EXPECT_TRUE(e.error_message().startsWith("Folder::create_file(): "));
    }
}

TEST(Runtime, runtime_destroyed_exceptions)
{
    // Gettting an account after shutting down the runtime must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        runtime->shutdown();
        try
        {
            acc->runtime();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Account::runtime(): Runtime was destroyed previously", e.error_message());
        }
    }

    // Getting an account after destroying the runtime must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        runtime.reset();
        try
        {
            acc->runtime();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Account::runtime(): Runtime was destroyed previously", e.error_message());
        }
    }

    // Getting the account from a root with a destroyed runtime must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        auto root = get_root(runtime);
        runtime.reset();
        try
        {
            root->account();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Root::account(): Runtime was destroyed previously", e.error_message());
        }
    }

    // Getting the account from a root with a destroyed account must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        auto root = get_root(runtime);
        runtime.reset();
        acc.reset();
        try
        {
            root->account();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Root::account(): Runtime was destroyed previously", e.error_message());
        }
    }

    // Getting the root from an item with a destroyed runtime must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        auto root = get_root(runtime);
        clear_folder(root);

        auto create_fut = root->create_file("file1", 0);
        ASSERT_TRUE(wait(create_fut));
        auto uploader = create_fut.result();
        auto finish_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(finish_fut));
        auto file = finish_fut.result();

        runtime.reset();
        try
        {
            file->root();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Item::root(): Runtime was destroyed previously", e.error_message());
        }
    }

    // Getting the root from an item with a destroyed root must fail.
    {
        auto runtime = Runtime::create();
        auto acc = get_account(runtime);
        auto root = get_root(runtime);
        clear_folder(root);

        auto create_fut = root->create_file("file1", 0);
        ASSERT_TRUE(wait(create_fut));
        auto uploader = create_fut.result();
        auto finish_fut = uploader->finish_upload();
        ASSERT_TRUE(wait(finish_fut));
        auto file = finish_fut.result();

        runtime.reset();
        qDebug() << "shared count:" << acc.use_count();
        acc.reset();
        qDebug() << "shared count:" << acc.use_count();
        root.reset();
        try
        {
            file->root();
            FAIL();
        }
        catch (RuntimeDestroyedException const& e)
        {
            EXPECT_EQ("Item::root(): Runtime was destroyed previously", e.error_message());
        }
    }

    // TODO: Lots more places to cover here.
}

int main(int argc, char** argv)
{
    boost::system::error_code ec;
    boost::filesystem::remove_all(TEST_DIR "/storage-framework", ec);
    setenv("STORAGE_FRAMEWORK_ROOT", TEST_DIR, true);

    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
