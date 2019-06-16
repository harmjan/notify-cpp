/*
 * Copyright (c) 2017 Erik Zenker <erikzenker@hotmail.com>
 * Copyright (c) 2018 Rafael Sadowski <rafael.sadowski@computacenter.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <notify-cpp/fanotify.h>
#include <notify-cpp/inotify.h>
#include <notify-cpp/notify_controller.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

/*
 * The test cases based on the original work from Erik Zenker for inotify-cpp.
 * In to guarantee a compatibility with inotify-cpp the tests were mostly
 * unchanged.
 * TODO Not all tests enabled yet
 */
using namespace notifycpp;

void openFile(std::filesystem::path file)
{
    std::ofstream stream;
    stream.open(file.string(), std::ifstream::out);
    BOOST_CHECK(stream.is_open());
    stream << "Writing this to a file.\n";
    stream.close();
}

struct NotifierBuilderTests {
    NotifierBuilderTests()
        : testDirectory_("testDirectory")
        , recursiveTestDirectory_(testDirectory_ / "recursiveTestDirectory")
        , testFileOne_(testDirectory_ / "test.txt")
        , testFileTwo_(testDirectory_ / "test2.txt")
        , timeout_(1)
    {
        std::filesystem::create_directories(testDirectory_);
        std::ofstream streamOne(testFileOne_);
        std::ofstream streamTwo(testFileTwo_);
    }

    ~NotifierBuilderTests() = default;

    std::filesystem::path testDirectory_;
    std::filesystem::path recursiveTestDirectory_;
    std::filesystem::path testFileOne_;
    std::filesystem::path testFileTwo_;

    std::chrono::seconds timeout_;

    // Events
    std::promise<Notification> promisedOpen_;
    std::promise<Notification> promisedCloseNoWrite_;
};

BOOST_AUTO_TEST_CASE(EventOperatorTest)
{
    BOOST_CHECK((Event::all & Event::close_write) == Event::close_write);
    BOOST_CHECK((Event::all & Event::moved_from) == Event::moved_from);
    BOOST_CHECK((Event::move & Event::moved_from) == Event::moved_from);
    BOOST_CHECK(!((Event::move & Event::open) == Event::open));
    BOOST_CHECK(toString(Event::access) == std::string("access"));
}

BOOST_FIXTURE_TEST_CASE(shouldNotAcceptNotExistingPaths, NotifierBuilderTests)
{
    BOOST_CHECK_THROW(InotifyController().watchPathRecursively(std::filesystem::path("/not/existing/path/")), std::invalid_argument);
    BOOST_CHECK_THROW( FanotifyController().watchPathRecursively(std::filesystem::path("/not/existing/path/")), std::invalid_argument);

    BOOST_CHECK_THROW(InotifyController().watchFile(std::filesystem::path("/not/existing/file")), std::invalid_argument);
    BOOST_CHECK_THROW( FanotifyController().watchFile(std::filesystem::path("/not/existing/file")), std::invalid_argument);
}

BOOST_FIXTURE_TEST_CASE(shouldNotifyOnOpenEvent, NotifierBuilderTests)
{
    NotifyController notifier = InotifyController().watchFile({testFileOne_, Event::close}).onEvent(Event::close, [&](Notification notification) {
        promisedOpen_.set_value(notification);
    });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);

    auto futureOpenEvent = promisedOpen_.get_future();
    BOOST_CHECK(futureOpenEvent.wait_for(timeout_) == std::future_status::ready);
    const auto notify = futureOpenEvent.get();
    BOOST_CHECK(notify.getEvent() == Event::close);
    BOOST_CHECK(notify.getPath() == testFileOne_);
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldNotifyOnMultipleEvents, NotifierBuilderTests)
{
    InotifyController notifier = InotifyController();

    Event watchOn = Event::open | Event::close_write;
    BOOST_CHECK((watchOn & Event::close_write) == Event::close_write);
    BOOST_CHECK((watchOn & Event::open) == Event::open);
    BOOST_CHECK((watchOn & Event::moved_from) != Event::moved_from);

    notifier.watchFile({testFileOne_, watchOn}).onEvents({Event::open, Event::close_write}, [&](Notification notification) {
        switch (notification.getEvent()) {
        case Event::open:
            promisedOpen_.set_value(notification);
            break;
        case Event::close_write:
            promisedCloseNoWrite_.set_value(notification);
            break;
        default:
            break;
        }
    });

    std::thread thread([&notifier]() {
        notifier.runOnce();
        notifier.runOnce();
    });

    openFile(testFileOne_);

    auto futureOpen = promisedOpen_.get_future();
    auto futureCloseNoWrite = promisedCloseNoWrite_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(futureOpen.get().getEvent() == Event::open);
    BOOST_CHECK(futureCloseNoWrite.wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(futureCloseNoWrite.get().getEvent() == Event::close_write);
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldStopRunOnce, NotifierBuilderTests)
{
    NotifyController notifier = InotifyController().watchFile(testFileOne_);

    std::thread thread([&notifier]() { notifier.runOnce(); });

    notifier.stop();

    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldStopRun, NotifierBuilderTests)
{
    InotifyController notifier = InotifyController();
    notifier.watchFile(testFileOne_);

    std::thread thread([&notifier]() { notifier.run(); });

    notifier.stop();

    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldIgnoreFileOnce, NotifierBuilderTests)
{
    /* XX
    InotifyController notifier = InotifyController();
    notifier.watchFile(testFileOne_).ignoreFileOnce(testFileOne_).onEvent(
        Event::open, [&](Notification notification) { promisedOpen_.set_value(notification); });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);

    auto futureOpen = promisedOpen_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) != std::future_status::ready);

    notifier.stop();
    thread.join();
    */
}

BOOST_FIXTURE_TEST_CASE(shouldIgnoreFile, NotifierBuilderTests)
{
    NotifyController notifier = InotifyController().ignore(testFileOne_).watchFile({testFileOne_, Event::close}).onEvent(Event::close, [&](Notification notification) {
        promisedOpen_.set_value(notification);
    });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);

    auto futureOpenEvent = promisedOpen_.get_future();
    BOOST_CHECK(futureOpenEvent.wait_for(timeout_) == std::future_status::timeout);
    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldWatchPathRecursively, NotifierBuilderTests)
{
    /* XXX
    InotifyController notifier = InotifyController();
    notifier.watchPathRecursively(testDirectory_)
                        .onEvent(Event::open, [&](Notification notification) {
                            switch (notification.getEvent()) {
                            case Event::open:
                                promisedOpen_.set_value(notification);
                                break;
                            }

                        });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);

    auto futureOpen = promisedOpen_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) == std::future_status::ready);

    notifier.stop();
    thread.join();
    */
}

BOOST_FIXTURE_TEST_CASE(shouldUnwatchPath, NotifierBuilderTests)
{
    std::promise<Notification> timeoutObserved;
    std::chrono::milliseconds timeout(100);

    InotifyController notifier = InotifyController();
    notifier.watchFile(testFileOne_).unwatch(testFileOne_);

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);
    BOOST_CHECK(promisedOpen_.get_future().wait_for(timeout_) != std::future_status::ready);
    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldCallUserDefinedUnexpectedExceptionObserver, NotifierBuilderTests)
{
    std::promise<void> observerCalled;

    NotifyController notifier2 = InotifyController().watchFile(testFileOne_).onUnexpectedEvent([&](Notification) { observerCalled.set_value(); });

    NotifyController notifier = InotifyController();
    notifier.watchFile(testFileOne_).onUnexpectedEvent([&](Notification) { observerCalled.set_value(); });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFileOne_);

    BOOST_CHECK(observerCalled.get_future().wait_for(timeout_) == std::future_status::ready);
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldSetEventTimeout, NotifierBuilderTests)
{
    /*
    std::promise<Notification> timeoutObserved;
    std::chrono::milliseconds timeout(100);

    InotifyController notifier = InotifyController();
    notifier.watchFile(testFileOne_)
              .onEvent(
                  Event::open,
                  [&](Notification notification) { promisedOpen_.set_value(notification); })
              .setEventTimeout(timeout, [&](Notification notification) {
                  timeoutObserved.set_value(notification);
              });

    std::thread thread([&notifier]() {
        notifier.runOnce(); // open
    });

    openFile(testFileOne_);

    BOOST_CHECK(promisedOpen_.get_future().wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(timeoutObserved.get_future().wait_for(timeout_) == std::future_status::ready);
    thread.join();
    */
}
