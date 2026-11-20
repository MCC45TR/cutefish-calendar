// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "scenario.h"

#include "download.h"
#include "holidayfeeds.h"
#include "subscriptions.h"

#include <ui/combo_box.h>
#include <ui/keys.h>
#include <ui/text_field.h>
#include <ui/theme.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace cutefish::ui;

using CalendarsTest = Scenario;

static std::vector<std::pair<std::string, std::string>> uriList(const std::string& list)
{
    return {{"text/uri-list", list}};
}

TEST_F(CalendarsTest, CalendarRowsShowAndHideCalendars)
{
    TestDriver& driver = open();
    const std::string row = "calendars:" + m_model->calendars().front().name;
    ASSERT_TRUE(driver.shows(row));
    driver.click(row);
    EXPECT_FALSE(m_model->calendars().front().visible);
    runPosted();
    driver.click(row);
    EXPECT_TRUE(m_model->calendars().front().visible);
}

TEST_F(CalendarsTest, NewCalendarIsCreatedFromTheSidebar)
{
    TestDriver& driver = open();
    ASSERT_TRUE(driver.shows("sidebar:addCalendar"));
    driver.click("sidebar:addCalendar");
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    dialog->typeText("Work");
    ASSERT_TRUE(dialog->shows("calendarDialog:save"));
    dialog->click("calendarDialog:save");
    dialog.reset();
    EXPECT_TRUE(closes(driver));
    ASSERT_EQ(m_model->calendars().size(), 2u);
    EXPECT_EQ(m_model->calendars().back().name, "Work");
}

TEST_F(CalendarsTest, TheCalendarDialogRenamesAndRecolours)
{
    const CalendarItem calendar = m_model->calendars().front();
    TestDriver& driver = open();
    driver.click("calendars:" + calendar.name, MouseButton::Right);
    driver.choosePopupItem("Edit…");
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    auto* name = dialog->find<TextField>("calendarDialog:name");
    View* save = dialog->find("calendarDialog:save");
    ASSERT_TRUE(name && save);
    EXPECT_EQ(name->text(), calendar.name);
    // The name starts selected, so typing replaces it; an empty one cannot be saved.
    dialog->typeKey(key::BackSpace);
    EXPECT_FALSE(save->isEnabled());
    dialog->typeText("Home");
    EXPECT_TRUE(save->isEnabled());

    size_t swatches = 0;
    for (const Color& color : {accent::blue, accent::purple, accent::pink, accent::red, accent::orange, accent::green,
                               accent::grey})
        swatches += dialog->shows("swatch:" + Rgb{color.r, color.g, color.b}.name());
    ASSERT_EQ(swatches, 7u);
    const Rgb green{accent::green.r, accent::green.g, accent::green.b};
    dialog->click("swatch:" + green.name());
    dialog->typeKey(key::Return);
    dialog.reset();
    EXPECT_TRUE(closes(driver));
    const CalendarItem changed = m_model->calendars().front();
    EXPECT_EQ(changed.name, "Home");
    EXPECT_EQ(changed.color, green);
}

TEST_F(CalendarsTest, DroppedCalendarFilesAreImported)
{
    const std::string file = m_dir + "/trip.ics";
    FILE* out = fopen(file.c_str(), "w");
    ASSERT_TRUE(out);
    fputs("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:trip-1\r\nDTSTAMP:20261001T000000Z\r\n"
          "SUMMARY:Trip\r\nDTSTART;VALUE=DATE:20261020\r\nDTEND;VALUE=DATE:20261021\r\nEND:VEVENT\r\n"
          "END:VCALENDAR\r\n",
          out);
    fclose(out);

    TestDriver& driver = open();
    // Only calendar files are taken.
    std::string refused;
    TestDriver refusing(driver.window(), [&refused](const std::string& message) { refused = message; });
    EXPECT_FALSE(refusing.drop("calendar:content", uriList("file:///tmp/notes.txt\r\n")));
    EXPECT_FALSE(refused.empty());
    runPosted();
    EXPECT_FALSE(driver.dialog());

    EXPECT_TRUE(driver.drop("calendar:content", uriList("# dropped\r\nfile://" + file + "\r\n")));
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(dialog->shows("import:import"));
    dialog->click("import:import");
    dialog.reset();
    EXPECT_TRUE(closes(driver));
    const Date day(2026, 10, 20);
    const std::vector<Occurrence> found = eventsOn(day);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found.front().summary, "Trip");
}

TEST_F(CalendarsTest, TheImportDialogAddsToTheChosenCalendar)
{
    const std::string other = m_model->createCalendar("Work", Rgb{1, 2, 3});
    m_model->setDefaultCalendar(other);
    std::string list;
    for (const auto& [name, uri] : {std::pair{"a.ics", "a.ics"}, std::pair{"b%20c.ics", "b%2520c.ics"}}) {
        std::ofstream(m_dir + "/" + name) << "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:" << name
                                          << "\r\nDTSTAMP:20261001T000000Z\r\nSUMMARY:Trip\r\n"
                                             "DTSTART;VALUE=DATE:20261020\r\nDTEND;VALUE=DATE:20261021\r\n"
                                             "END:VEVENT\r\nEND:VCALENDAR\r\n";
        list += "file://" + m_dir + "/" + uri + "\r\n";
    }

    TestDriver& driver = open();
    EXPECT_TRUE(driver.drop("calendar:content", uriList(list)));
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->showsText("2 files"));
    auto* box = dialog->find<ComboBox>("import:calendar");
    ASSERT_TRUE(box);
    EXPECT_EQ(box->currentText(), "Work");

    dialog->typeKey(key::Return);
    dialog.reset();
    EXPECT_TRUE(closes(driver));
    const std::vector<Occurrence> found = eventsOn(Date(2026, 10, 20));
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found.front().calendarId, other);
    EXPECT_EQ(found.back().calendarId, other);
}

TEST_F(CalendarsTest, OneFileIsNamedWithoutItsEscapes)
{
    TestDriver& driver = open();
    EXPECT_TRUE(driver.drop("calendar:content", uriList("file:///tmp/My%20Trip.ics\r\n")));
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(dialog->showsText("My Trip.ics"));
    dialog->typeKey(key::Escape);
    dialog.reset();
    EXPECT_TRUE(closes(driver));
}

static const char HolidayEvents[] = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:Holidays\r\n"
                                  "BEGIN:VEVENT\r\nUID:national-day\r\nDTSTART;VALUE=DATE:20261001\r\n"
                                  "DTEND;VALUE=DATE:20261008\r\nSUMMARY:National Day\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

TEST_F(CalendarsTest, TheSubscriptionDialogSubscribesOrSaysWhyNot)
{
    long status = 404;
    m_model->subscriptions()->setFetch([&status](const std::string&, const std::string&, const std::string&) {
        Download answer;
        answer.status = status;
        answer.body = status == 200 ? HolidayEvents : "";
        return answer;
    });
    TestDriver& driver = open();
    EXPECT_FALSE(driver.shows("sidebar:subscriptions"));

    driver.chooseMenuItem("File", "New Subscription…");
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    auto* url = dialog->find<TextField>("subscription:url");
    View* subscribe = dialog->find("subscription:subscribe");
    ASSERT_TRUE(url && subscribe);
    EXPECT_FALSE(subscribe->isEnabled());
    EXPECT_FALSE(dialog->shows("subscription:error"));

    dialog->typeText("not an address");
    dialog->typeKey(key::Return);
    ASSERT_TRUE(dialog->shows("subscription:error"));
    EXPECT_EQ(dialog->text("subscription:error"), "This is not a web address.");

    // The server's answer keeps the dialog open; typing puts the reason away.
    dialog->click("subscription:url");
    dialog->typeKey(key::letter('a'), ControlModifier);
    dialog->typeText("webcal://example.com/holidays.ics");
    EXPECT_FALSE(dialog->shows("subscription:error"));
    dialog->click("subscription:subscribe");
    // Busy: the address cannot change meanwhile.
    EXPECT_FALSE(url->isEnabled());
    EXPECT_FALSE(subscribe->isEnabled());
    ASSERT_TRUE(TestDriver::waitFor([&] { return url->isEnabled(); }));
    dialog->layout();
    ASSERT_TRUE(dialog->shows("subscription:error"));
    EXPECT_EQ(dialog->text("subscription:error"), "The server answered with error 404.");
    EXPECT_TRUE(subscribe->isEnabled());

    status = 200;
    dialog->click("subscription:subscribe");
    dialog.reset();
    ASSERT_TRUE(closes(driver));
    std::string id;
    for (const CalendarItem& calendar : m_model->calendars()) {
        if (calendar.readOnly)
            id = calendar.id;
    }
    ASSERT_FALSE(id.empty());
    EXPECT_EQ(m_store->calendar(id).name, "Holidays");
    EXPECT_EQ(m_store->eventCount(id), 1);

    // Its own section in the sidebar, under the user's calendars.
    runPosted();
    driver.layout();
    ASSERT_TRUE(driver.shows("sidebar:calendars") && driver.shows("sidebar:subscriptionsHeading")
                && driver.shows("sidebar:subscriptions"));
    EXPECT_TRUE(driver.shows("subscriptions:Holidays"));
}

TEST_F(CalendarsTest, PublicHolidaysArePickedFromAList)
{
    const std::vector<HolidayFeed> feeds = HolidayFeeds::list();
    ASSERT_GE(feeds.size(), 2u);
    m_store->createSubscription(feeds[1].name, Rgb{0xff, 0, 0}, {.url = feeds[1].url});
    std::string fetched;
    m_model->subscriptions()->setFetch([&fetched](const std::string& url, const std::string&, const std::string&) {
        fetched = url;
        Download answer;
        answer.status = 200;
        answer.body = HolidayEvents;
        return answer;
    });

    TestDriver& driver = open();
    driver.chooseMenuItem("File", "New Subscription…");
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    EXPECT_FALSE(dialog->shows("subscription:holidays"));

    dialog->click("subscription:holidaysToggle");
    EXPECT_TRUE(dialog->shows("subscription:holidays"));
    size_t rows = 0;
    for (const HolidayFeed& feed : feeds)
        rows += dialog->shows("holidays:" + feed.url);
    ASSERT_EQ(rows, feeds.size());

    auto* url = dialog->find<TextField>("subscription:url");
    ASSERT_TRUE(url);
    // A region already subscribed to is not picked again.
    dialog->click("holidays:" + feeds[1].url);
    EXPECT_EQ(url->text(), "");
    dialog->click("holidays:" + feeds[0].url);
    EXPECT_EQ(url->text(), feeds[0].url);
    EXPECT_TRUE(dialog->find("subscription:subscribe")->isEnabled());

    dialog->click("subscription:subscribe");
    dialog.reset();
    ASSERT_TRUE(closes(driver));
    EXPECT_EQ(fetched, feeds[0].url);
    int subscriptions = 0;
    for (const CalendarInfo& calendar : m_store->calendars())
        subscriptions += calendar.isReadOnly() && calendar.url == feeds[0].url;
    EXPECT_EQ(subscriptions, 1);
}

TEST_F(CalendarsTest, SettingsChangeTheWeek)
{
    TestDriver& driver = open();
    driver.chooseMenuItem("Calendar", "Settings…");
    std::unique_ptr<TestDriver> settings = dialogOver(driver);
    ASSERT_TRUE(settings);
    auto* weekStart = settings->find<ComboBox>("settings:weekStart");
    ASSERT_TRUE(weekStart);
    ASSERT_EQ(weekStart->items().size(), 4u);
    EXPECT_EQ(weekStart->items()[0], "System Default");
    EXPECT_EQ(weekStart->items()[2], "Monday");
    EXPECT_EQ(weekStart->currentIndex(), 0);
    choose(*settings, "settings:weekStart", 2);
    EXPECT_EQ(m_model->weekStart(), 1);

    ASSERT_TRUE(settings->shows("settings:weekNumbers"));
    settings->click("settings:weekNumbers");
    EXPECT_TRUE(m_model->showWeekNumbers());

    settings->typeKey(key::Escape);
    settings.reset();
    EXPECT_TRUE(closes(driver));
}
