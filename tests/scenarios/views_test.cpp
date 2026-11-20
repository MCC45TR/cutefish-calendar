// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "scenario.h"

#include "ui/searchresults.h"

#include <ui/keys.h>
#include <ui/menu.h>
#include <ui/text_field.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>

using namespace cutefish::ui;

using ViewsTest = Scenario;

// The first day of what the view shows, the year view's by its title.
static Date shownRange(TestDriver& driver, const std::string& view)
{
    if (view == "day" || view == "week") {
        const TimeGridView* grid = driver.find<TimeGridView>("view:" + view);
        return grid ? grid->rangeStart() : Date();
    }
    if (view == "month") {
        const MonthView* month = driver.find<MonthView>("view:month");
        return month ? month->gridStart() : Date();
    }
    return Date(std::stoi(driver.text("navigation:title")), 1, 1);
}

TEST_F(ViewsTest, EveryViewStepsAndComesBack)
{
    for (const bool dark : {false, true}) {
        Theme::setCurrent(dark ? Theme::dark() : Theme::light());
        for (const char* view : {"day", "week", "month", "year"}) {
            m_model->setView(view);
            TestDriver& driver = open();
            const std::string name = std::string("view:") + view;
            EXPECT_EQ(driver.countShown(name), 1u) << view;
            EXPECT_EQ(driver.countShown("navigation:title"), 1u);

            // Every view steps forward and back to where it was.
            const Date range = shownRange(driver, view);
            driver.click("navigation:next");
            EXPECT_GT(shownRange(driver, view), range) << view;
            driver.click("navigation:previous");
            EXPECT_EQ(shownRange(driver, view), range) << view;
        }
    }
}

TEST_F(ViewsTest, EventsShowInEveryView)
{
    const Date today = Date::currentDate();
    addEvent("Planning", today, 10 * 60, 90);
    addEvent("Trip", today, 0, 0, true);

    m_model->setView("month");
    TestDriver& driver = open();
    EXPECT_EQ(driver.countShown("event:line"), 2u);

    driver.chooseMenuItem("View", "Week");
    EXPECT_EQ(driver.countShown("event:block"), 1u);
    EXPECT_EQ(driver.countShown("event:line"), 1u);

    driver.chooseMenuItem("View", "Day");
    EXPECT_EQ(driver.countShown("event:block"), 1u);
}

TEST_F(ViewsTest, SearchFindsEvent)
{
    addEvent("Piano lesson", Date::currentDate().addDays(3), 9 * 60, 60);
    TestDriver& driver = open();

    driver.click("header:search");
    driver.typeText("piano");
    ASSERT_TRUE(driver.shows("search:results"));
    EXPECT_EQ(driver.find<SearchResults>("search:results")->results().size(), 1u);

    // Escape clears the search and returns to the view.
    driver.typeKey(key::Escape);
    EXPECT_FALSE(driver.shows("search:results"));
}

TEST_F(ViewsTest, SidebarButtonHidesAndShowsTheSidebar)
{
    TestDriver& driver = open();
    EXPECT_TRUE(driver.shows("sidebar:panel"));

    driver.click("header:sidebar");
    EXPECT_TRUE(TestDriver::waitFor([&] {
        driver.layout();
        return !driver.shows("sidebar:panel");
    }));
    EXPECT_FALSE(m_model->sidebarVisible());
    driver.click("header:sidebar");
    EXPECT_TRUE(TestDriver::waitFor([&] {
        driver.layout();
        return driver.shows("sidebar:panel");
    }));
}

TEST_F(ViewsTest, KeysMoveTheDayAndShortcutsSwitchViews)
{
    m_model->setView("month");
    TestDriver& driver = open();
    const Date start = selectedDay(driver, "Month");
    driver.typeKey(key::Right);
    EXPECT_EQ(selectedDay(driver, "Month"), start.addDays(1));
    driver.typeKey(key::Down);
    EXPECT_EQ(selectedDay(driver, "Month"), start.addDays(8));
    driver.typeKey(key::Home);
    EXPECT_EQ(selectedDay(driver, "Month"), m_model->today());

    driver.typeKey('2', ControlModifier);
    EXPECT_TRUE(driver.shows("view:week"));
    driver.typeKey(key::PageDown);
    EXPECT_EQ(selectedDay(driver, "Week"), m_model->today().addDays(7));
    driver.typeKey(key::Left, AltModifier);
    EXPECT_EQ(selectedDay(driver, "Week"), m_model->today());
    driver.typeKey('4', ControlModifier);
    EXPECT_TRUE(driver.shows("view:year"));
    driver.typeKey('3', ControlModifier);
    EXPECT_TRUE(driver.shows("view:month"));
}

TEST_F(ViewsTest, WeekNumbersFollowTheSetting)
{
    m_model->setView("month");
    TestDriver& driver = open();
    EXPECT_EQ(driver.countShown("month:weekNumber"), 0u);

    driver.chooseMenuItem("Calendar", "Settings…");
    std::unique_ptr<TestDriver> settings = dialogOver(driver);
    ASSERT_TRUE(settings);
    settings->click("settings:weekNumbers");
    driver.layout();
    EXPECT_EQ(driver.countShown("month:weekNumber"), 6u);
    settings->typeKey(key::Escape);
    settings.reset();
    EXPECT_TRUE(closes(driver));
}

TEST_F(ViewsTest, WindowShowsItsMenusInTheMenuBar)
{
    TestDriver& driver = open();
    Menu* menu = driver.window().mainMenu();
    ASSERT_TRUE(menu);
    const std::vector<MenuRole> roles{MenuRole::Application, MenuRole::None, MenuRole::None, MenuRole::None,
                                      MenuRole::Windows};
    for (size_t i = 0; i < roles.size(); ++i) {
        ASSERT_TRUE(menu->submenu(i)) << i;
        EXPECT_EQ(menu->submenu(i)->role(), roles[i]) << i;
    }
    EXPECT_FALSE(menu->submenu(roles.size()));

    Application::instance()->languageChanged.emit();
    driver.layout();
    ASSERT_TRUE(driver.window().mainMenu());
    EXPECT_EQ(driver.window().mainMenu()->submenu(0)->role(), MenuRole::Application);
}

TEST_F(ViewsTest, LanguageChangeKeepsWhereTheUserWas)
{
    const Date day = Date::currentDate().addDays(3);
    addEvent("Piano lesson", day, 9 * 60, 60);
    m_model->setView("week");
    TestDriver& driver = open();
    const std::vector<EventItem> found = m_model->search("Piano");
    ASSERT_EQ(found.size(), 1u);

    // The event found and selected, the sidebar put away, a search typed and
    // a new event being written.
    driver.click("header:search");
    driver.typeText("piano");
    driver.click("search:result");
    driver.click("header:sidebar");
    driver.click("header:search");
    driver.typeKey(key::letter('a'), ControlModifier);
    driver.typeText("piano");
    driver.chooseMenuItem("File", "New Event");
    std::unique_ptr<TestDriver> editor = dialogOver(driver);
    ASSERT_TRUE(editor);
    Window* editorWindow = &editor->window();
    const View* titleBefore = editor->find("editor:title");
    editor->typeText("Dentist");
    ASSERT_TRUE(TestDriver::waitFor([&] {
        driver.layout();
        return !driver.shows("sidebar:panel");
    }));
    const View* searchBefore = driver.find("header:search");

    Application::instance()->languageChanged.emit();
    driver.layout();
    ASSERT_EQ(driver.countShown("header:search"), 1u);
    EXPECT_NE(driver.find("header:search"), searchBefore);
    EXPECT_TRUE(driver.shows("view:week"));
    EXPECT_FALSE(m_model->sidebarVisible());
    EXPECT_FALSE(driver.shows("sidebar:panel"));
    EXPECT_EQ(driver.text("header:search"), "piano");
    ASSERT_TRUE(driver.shows("search:results"));
    EXPECT_EQ(driver.find<SearchResults>("search:results")->results().size(), 1u);

    // The editor stays open with what was typed, in views made again.
    ASSERT_EQ(driver.dialog(), editorWindow);
    editor->layout();
    ASSERT_EQ(editor->countShown("editor:title"), 1u);
    EXPECT_NE(editor->find("editor:title"), titleBefore);
    EXPECT_EQ(editor->text("editor:title"), "Dentist");
    editor->click("editor:save");
    editor.reset();
    EXPECT_TRUE(closes(driver));
    const std::vector<Occurrence> saved = eventsOn(day);
    ASSERT_EQ(saved.size(), 2u);
    EXPECT_TRUE(saved[0].summary == "Dentist" || saved[1].summary == "Dentist");

    // Still on the day, with the event found selected.
    EXPECT_EQ(selectedDay(driver, "Week"), day);
    driver.chooseMenuItem("Edit", "Edit Event…");
    std::unique_ptr<TestDriver> selected = dialogOver(driver);
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->text("editor:title"), "Piano lesson");
}
