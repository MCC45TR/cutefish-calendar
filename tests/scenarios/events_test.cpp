// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "scenario.h"

#include "eventfile.h"
#include "localedata.h"

#include <ui/combo_box.h>
#include <ui/keys.h>

#include <gtest/gtest.h>

#include <string>

using namespace cutefish::ui;

using EventsTest = Scenario;

TEST_F(EventsTest, EditorCreatesEvent)
{
    TestDriver& driver = open();
    driver.chooseMenuItem("File", "New Event");
    std::unique_ptr<TestDriver> editor = dialogOver(driver);
    ASSERT_TRUE(editor);
    ASSERT_EQ(editor->countShown("editor:title"), 1u);
    const std::string start = editor->find<ComboBox>("editor:startTime")->editText();
    editor->click("editor:title");
    editor->typeText("Dentist");
    editor->click("editor:save");
    editor.reset();
    EXPECT_TRUE(closes(driver));

    const std::vector<Occurrence> today = eventsOn(Date::currentDate());
    ASSERT_EQ(today.size(), 1u);
    EXPECT_EQ(today.front().summary, "Dentist");
    EXPECT_TRUE(today.front().start.date() == Date::currentDate()
                && LocaleData::timeText(today.front().start.time()) == start)
            << start;
}

TEST_F(EventsTest, SelectedEventIsEditedAndDeletedFromTheKeyboard)
{
    const Date tomorrow = Date::currentDate().addDays(1);
    addEvent("Review", tomorrow, 9 * 60, 60);
    m_model->setView("day");
    TestDriver& driver = open();
    driver.click("navigation:next");
    ASSERT_EQ(driver.countShown("event:block"), 1u);

    driver.click("event:block");
    driver.typeKey(key::Return);
    std::unique_ptr<TestDriver> editor = dialogOver(driver);
    ASSERT_TRUE(editor);
    EXPECT_EQ(editor->text("editor:title"), "Review");
    editor->typeKey(key::Escape);
    editor.reset();
    ASSERT_TRUE(closes(driver));

    driver.typeKey(key::Delete);
    runPosted();
    EXPECT_TRUE(eventsOn(tomorrow).empty());
    EXPECT_TRUE(m_model->canUndo());
    driver.chooseMenuItem("Edit", "Undo Delete");
    EXPECT_EQ(eventsOn(tomorrow).size(), 1u);

    pauseBetweenClicks();
    driver.click("event:block");
    driver.typeKey(key::Escape);
    // Nothing selected, so Return edits nothing.
    driver.typeKey(key::Return);
    runPosted();
    EXPECT_FALSE(driver.dialog());
}

TEST_F(EventsTest, RepeatingEventsAskWhichOccurrences)
{
    const Date today = Date::currentDate();
    const DateTime start(today, Time(9, 0));
    EventForm form = m_model->newEventData(start, start.addSecs(1800), false);
    form.title = "Standup";
    form.repeat = Repeat::Daily;
    ASSERT_TRUE(m_model->saveEvent(form, EventFile::AllEvents));
    m_model->setView("day");
    TestDriver& driver = open();

    // The second of the series goes.
    ASSERT_EQ(eventsOn(today, 3).size(), 3u);
    driver.click("navigation:next");
    driver.click("event:block");
    driver.typeKey(key::Delete);
    std::unique_ptr<TestDriver> dialog = dialogOver(driver);
    ASSERT_TRUE(dialog);
    ASSERT_EQ(dialog->countShown("scope:thisEvent"), 1u);
    dialog->click("scope:thisEvent");
    dialog.reset();
    EXPECT_TRUE(closes(driver));
    EXPECT_EQ(eventsOn(today, 3).size(), 2u);
    EXPECT_EQ(m_model->undoText(), "Undo Delete");
}

TEST_F(EventsTest, ASubscribedEventOpensReadOnly)
{
    const Date today = Date::currentDate();
    const std::string events = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:Holidays\r\n"
                               "BEGIN:VEVENT\r\nUID:national-day\r\nDTSTART;VALUE=DATE:"
            + today.toCompactString() + "\r\nDTEND;VALUE=DATE:" + today.addDays(1).toCompactString()
            + "\r\nSUMMARY:National Day\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    const std::string id = m_store->createSubscription("Holidays", Rgb{0xff, 0, 0},
                                                       {.url = "https://example.com/holidays.ics"});
    ASSERT_EQ(m_store->replaceEvents(id, EventFile::split(events)), 1);
    const std::string key = m_store->keys().front().starts_with(id) ? m_store->keys().front() : m_store->keys().back();

    m_model->setView("month");
    TestDriver& driver = open();
    driver.click("event:line");
    driver.typeKey(key::Return);
    std::unique_ptr<TestDriver> editor = dialogOver(driver);
    ASSERT_TRUE(editor);
    EXPECT_FALSE(editor->shows("editor:save"));
    EXPECT_FALSE(editor->shows("editor:delete"));
    ASSERT_EQ(editor->countShown("editor:title"), 1u);
    EXPECT_EQ(editor->text("editor:title"), "National Day");
    editor->typeText("Changed");
    EXPECT_EQ(editor->text("editor:title"), "National Day");
    EXPECT_FALSE(editor->find("editor:allDay")->isEnabled());
    auto* calendar = editor->find<ComboBox>("editor:calendar");
    ASSERT_TRUE(calendar);
    EXPECT_FALSE(calendar->isEnabled());
    EXPECT_EQ(calendar->currentText(), "Holidays");

    editor->typeKey(key::Return, ControlModifier);
    editor->typeKey(key::Escape);
    editor.reset();
    EXPECT_TRUE(closes(driver));
    EXPECT_EQ(m_store->event(key)->summary(), "National Day");

    // Nor does the keyboard delete it.
    pauseBetweenClicks();
    driver.click("event:line");
    driver.typeKey(key::Delete);
    runPosted();
    EXPECT_TRUE(m_store->event(key));
    EXPECT_FALSE(driver.dialog());
}
