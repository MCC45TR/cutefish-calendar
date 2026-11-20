// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "scenario.h"

#include "eventfile.h"
#include "localedata.h"
#include "ui/dialogs.h"

#include <ui/combo_box.h>
#include <ui/keys.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using namespace cutefish::ui;

using EditorTest = Scenario;

static std::string timeText(int minute)
{
    return LocaleData::timeText(Time(minute / 60, minute % 60));
}

TEST_F(EditorTest, TypedTimesMoveTheEvent)
{
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    auto* box = editor->find<ComboBox>("editor:startTime");
    ASSERT_TRUE(box);
    ASSERT_TRUE(box->isEditable());
    typeOver(*editor, "editor:startTime", "9:40pm");
    // The editor keeps the hour's length and shows the time it read.
    EXPECT_EQ(box->editText(), timeText(21 * 60 + 40));
    EXPECT_EQ(box->currentIndex(), -1);
    EXPECT_EQ(editor->find<ComboBox>("editor:endTime")->editText(), timeText(22 * 60 + 40));
}

// What the start shows after each text typed over seven o'clock: the time
// read, or seven still for a text that reads as none.
static void expectTypedTime(TestDriver& editor, const std::string& text, int minute)
{
    Scenario::typeOver(editor, "editor:startTime", "7");
    Scenario::typeOver(editor, "editor:startTime", text);
    EXPECT_EQ(editor.find<ComboBox>("editor:startTime")->editText(), timeText(minute < 0 ? 7 * 60 : minute)) << text;
}

TEST_F(EditorTest, ReadsTypedTimes)
{
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    expectTypedTime(*editor, "9", 9 * 60);
    expectTypedTime(*editor, "930", 9 * 60 + 30);
    expectTypedTime(*editor, "9:30", 9 * 60 + 30);
    expectTypedTime(*editor, " 21:30 ", 21 * 60 + 30);
    expectTypedTime(*editor, "9:30 PM", 21 * 60 + 30);
    expectTypedTime(*editor, "9pm", 21 * 60);
    expectTypedTime(*editor, "12am", 0);
    expectTypedTime(*editor, "12 pm", 12 * 60);
    expectTypedTime(*editor, "\u4e0b\u53483:15", -1);
    expectTypedTime(*editor, "3:15\u4e0b\u5348", 15 * 60 + 15);
    expectTypedTime(*editor, "24:00", -1);
    expectTypedTime(*editor, "noon", -1);
}

TEST_F(EditorTest, EveryListedTimeReadsBack)
{
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    // The list's texts, with the locale's narrow space before am and pm.
    for (int minute = 0; minute < 24 * 60; minute += 15) {
        if (minute != 7 * 60)
            expectTypedTime(*editor, timeText(minute), minute);
    }
    expectTypedTime(*editor, "8:00\u202fam", 8 * 60);
    expectTypedTime(*editor, "8:30\u00a0PM", 20 * 60 + 30);
    expectTypedTime(*editor, "20:30:00", 20 * 60 + 30);
}

TEST_F(EditorTest, EscapeClosesTheDatePickerBeforeTheEditor)
{
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    auto* date = editor->find<DateButton>("editor:startDate");
    ASSERT_TRUE(date);
    editor->click("editor:startDate");
    EXPECT_TRUE(date->isPopupOpen());
    editor->typeKey(key::Escape);
    runPosted();
    EXPECT_FALSE(date->isPopupOpen());
    ASSERT_EQ(driver.dialog(), &editor->window());
    editor->typeKey(key::Escape);
    editor.reset();
    EXPECT_TRUE(closes(driver));
}

TEST_F(EditorTest, RepeatEndsAfterTheCountTyped)
{
    const Date day = Date::currentDate().addDays(1);
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    editor->typeText("Standup");
    EXPECT_FALSE(editor->shows("editor:repeatEnd"));

    choose(*editor, "editor:repeat", 1);
    ASSERT_TRUE(editor->shows("editor:repeatEnd"));
    EXPECT_FALSE(editor->shows("editor:repeatCount"));
    choose(*editor, "editor:repeatEnd", 2);
    ASSERT_TRUE(editor->shows("editor:repeatCount"));
    EXPECT_EQ(editor->text("editor:repeatCount"), "10");
    editor->click("editor:repeatCount");
    editor->typeKey(key::letter('a'), ControlModifier);
    // Only digits, at most three.
    editor->typeText("x3");
    EXPECT_EQ(editor->text("editor:repeatCount"), "3");

    editor->click("editor:title");
    editor->typeKey(key::Return, ControlModifier);
    editor.reset();
    EXPECT_TRUE(closes(driver));
    const std::vector<Occurrence> found = eventsOn(day, 10);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found.front().summary, "Standup");
    EXPECT_EQ(found.back().start.date(), day.addDays(2));
}

TEST_F(EditorTest, AnEndBeforeTheStartCannotBeSaved)
{
    const Date day = Date::currentDate().addDays(1);
    const std::string error = "The event ends before it starts.";
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    // From seven to eight, so an end three rows of the list later is past the start.
    typeOver(*editor, "editor:startTime", "7");
    EXPECT_FALSE(editor->showsText(error));
    View* save = editor->find("editor:save");
    ASSERT_TRUE(save);
    EXPECT_TRUE(save->isEnabled());

    auto* end = editor->find<ComboBox>("editor:endTime");
    ASSERT_TRUE(end);
    typeOver(*editor, "editor:endTime", "6:30");
    editor->layout();
    EXPECT_EQ(end->editText(), timeText(6 * 60 + 30));
    EXPECT_FALSE(save->isEnabled());
    EXPECT_TRUE(editor->showsText(error));

    editor->typeKey(key::Return, ControlModifier);
    runPosted();
    ASSERT_EQ(driver.dialog(), &editor->window());
    EXPECT_TRUE(eventsOn(day).empty());

    // A later end from the list makes it whole again.
    const std::string later = end->items()[(7 * 60 + 15) / 15];
    editor->typeKey(key::Down);
    editor->choosePopupItem(later);
    EXPECT_EQ(end->editText(), later);
    EXPECT_TRUE(save->isEnabled());
    EXPECT_FALSE(editor->showsText(error));
}

TEST_F(EditorTest, AllDayHidesTheTimesAndListsDayAlerts)
{
    const Date day = Date::currentDate().addDays(1);
    TestDriver& driver = open();
    std::unique_ptr<TestDriver> editor = newEventTomorrow(driver);
    ASSERT_TRUE(editor);
    auto* alerts = editor->find<ComboBox>("editor:alert");
    ASSERT_TRUE(alerts);
    EXPECT_EQ(alerts->items().size(), 11u);
    EXPECT_TRUE(editor->shows("editor:startTime"));

    ASSERT_TRUE(editor->shows("editor:allDay"));
    editor->click("editor:allDay");
    // The switch took the focus, as buttons do on a click.
    EXPECT_TRUE(editor->hasFocus("editor:allDay"));
    EXPECT_FALSE(editor->shows("editor:startTime"));
    EXPECT_FALSE(editor->shows("editor:endTime"));
    ASSERT_EQ(alerts->items().size(), 5u);
    EXPECT_EQ(alerts->items()[1], "On the Day (9:00)");
    choose(*editor, "editor:alert", 2);

    editor->click("editor:title");
    editor->typeText("Holiday");
    editor->typeKey(key::Return);
    editor.reset();
    EXPECT_TRUE(closes(driver));
    const std::vector<Occurrence> found = eventsOn(day);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_TRUE(found.front().allDay);
    const std::optional<EventForm> form = m_model->eventData(found.front().key, found.front().recurrenceKey);
    ASSERT_TRUE(form);
    EXPECT_EQ(form->alarm, int(EventData::OffsetAlarm));
    EXPECT_EQ(form->alarmOffset, -15 * 60);
}

TEST_F(EditorTest, ChangingTheRuleOfARepeatingEventAsksOnlyForLaterOnes)
{
    const Date day = Date::currentDate();
    const DateTime start(day, Time(9, 0));
    EventForm form = m_model->newEventData(start, start.addSecs(1800), false);
    form.title = "Standup";
    form.repeat = Repeat::Daily;
    ASSERT_TRUE(m_model->saveEvent(form, EventFile::AllEvents));
    ASSERT_EQ(eventsOn(day.addDays(1)).size(), 1u);

    m_model->setView("day");
    TestDriver& driver = open();
    driver.click("navigation:next");
    driver.click("event:block");
    driver.typeKey(key::Return);
    std::unique_ptr<TestDriver> editor = dialogOver(driver);
    ASSERT_TRUE(editor);
    EXPECT_TRUE(editor->shows("editor:delete"));
    choose(*editor, "editor:repeat", 3);
    editor->typeKey(key::Return, ControlModifier);
    std::unique_ptr<TestDriver> scope = dialogOver(*editor);
    ASSERT_TRUE(scope);
    EXPECT_FALSE(scope->shows("scope:thisEvent"));
    ASSERT_TRUE(scope->shows("scope:laterEvents"));
    EXPECT_TRUE(scope->hasFocus("scope:laterEvents"));
    scope->typeKey(key::Escape);
    scope.reset();
    EXPECT_TRUE(closes(*editor));
    EXPECT_EQ(driver.dialog(), &editor->window());
}
