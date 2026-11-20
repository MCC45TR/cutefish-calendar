// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "calendarmodel.h"
#include "calendarstore.h"
#include "ui/calendarwindow.h"
#include "ui/monthview.h"
#include "ui/timegridview.h"

#include <base/event_loop.h>
#include <ui/application.h>
#include <ui/combo_box.h>
#include <ui/keys.h>
#include <ui/test_driver.h>
#include <ui/theme.h>
#include <ui/window.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// A calendar directory of its own and the main window over it, worked only
// through drivers. The model and the store set up what is there before the
// window opens and tell what was saved after.
class Scenario : public testing::Test
{
public:
    // A driver of the newest dialog over the window the driver works.
    static std::unique_ptr<cutefish::ui::TestDriver> dialogOver(cutefish::ui::TestDriver& driver)
    {
        runPosted();
        cutefish::ui::Window* window = driver.dialog();
        if (!window) {
            ADD_FAILURE() << "no dialog over the window";
            return nullptr;
        }
        auto dialog = std::make_unique<cutefish::ui::TestDriver>(*window, failure);
        dialog->layout();
        return dialog;
    }

    // The dialogs over the window went, as after a close or a choice.
    static bool closes(cutefish::ui::TestDriver& driver)
    {
        return cutefish::ui::TestDriver::waitFor([&driver] { return !driver.dialog(); });
    }

    // What the loop has queued, as it runs between a user's inputs.
    static void runPosted()
    {
        cutefish::base::EventLoop& loop = cutefish::ui::Application::instance()->loop();
        loop.post([&loop] { loop.quit(); });
        loop.run();
    }

    // Longer than a double click lasts, so the next click on the same place
    // is a click of its own.
    static void pauseBetweenClicks()
    {
        cutefish::ui::TestDriver::waitFor([] { return false; }, std::chrono::milliseconds(450));
    }

    // The day selected, as the day view shows it; the View menu item of the
    // view to go back to.
    static Date selectedDay(cutefish::ui::TestDriver& driver, const std::string& backTo)
    {
        driver.chooseMenuItem("View", "Day");
        const TimeGridView* day = driver.find<TimeGridView>("view:day");
        const Date date = day ? day->rangeStart() : Date();
        driver.chooseMenuItem("View", backTo);
        return date;
    }

    // A new event for tomorrow, nine to ten, from the File menu of the day
    // view; the editor's driver.
    static std::unique_ptr<cutefish::ui::TestDriver> newEventTomorrow(cutefish::ui::TestDriver& driver)
    {
        driver.chooseMenuItem("View", "Day");
        driver.click("navigation:next");
        driver.chooseMenuItem("File", "New Event");
        return dialogOver(driver);
    }

    // An item of the box's list, picked from the list it opens.
    static void choose(cutefish::ui::TestDriver& dialog, const std::string& box, size_t index)
    {
        const cutefish::ui::ComboBox* combo = dialog.find<cutefish::ui::ComboBox>(box);
        ASSERT_TRUE(combo);
        ASSERT_LT(index, combo->items().size());
        const std::string item = combo->items()[index];
        dialog.click(box);
        dialog.choosePopupItem(item);
    }

    // Typed over what the editable box shows, and taken with Return.
    static void typeOver(cutefish::ui::TestDriver& dialog, const std::string& box, const std::string& text)
    {
        dialog.click(box);
        dialog.typeKey(cutefish::ui::key::letter('a'), cutefish::ui::ControlModifier);
        dialog.typeText(text);
        dialog.typeKey(cutefish::ui::key::Return);
    }

protected:
    void SetUp() override
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "calendar-scenario-XXXXXX").string();
        m_dir = mkdtemp(pattern.data());
        m_store = std::make_unique<CalendarStore>(m_dir + "/calendars");
        m_model = std::make_unique<CalendarModel>(m_store.get(), m_dir + "/calendar.conf");
        m_store->load();
    }

    void TearDown() override
    {
        m_driver.reset();
        m_window.reset();
        m_model.reset();
        m_store.reset();
        cutefish::ui::Theme::setCurrent(cutefish::ui::Theme::light());
        std::error_code error;
        std::filesystem::remove_all(m_dir, error);
    }

    // The window at its default size, shown.
    cutefish::ui::TestDriver& open()
    {
        m_driver.reset();
        m_window = std::make_unique<CalendarWindow>(m_model.get(), false);
        m_window->show();
        m_driver = std::make_unique<cutefish::ui::TestDriver>(*m_window->window(), failure);
        m_driver->layout();
        return *m_driver;
    }

    // An event of the default calendar.
    std::string addEvent(const std::string& title, Date day, int startMinute, int minutes, bool allDay = false)
    {
        const DateTime start(day, Time(startMinute / 60, startMinute % 60));
        EventForm form = m_model->newEventData(start, allDay ? DateTime(day, Time(0, 0)) : start.addSecs(minutes * 60),
                                              allDay);
        form.title = title;
        EXPECT_TRUE(m_model->saveEvent(form, EventFile::AllEvents));
        return title;
    }

    std::vector<Occurrence> eventsOn(Date day, int days = 1) const { return m_store->occurrences(day, day.addDays(days)); }

    std::string m_dir;
    std::unique_ptr<CalendarStore> m_store;
    std::unique_ptr<CalendarModel> m_model;

private:
    static void failure(const std::string& message) { ADD_FAILURE() << message; }

    std::unique_ptr<CalendarWindow> m_window;
    std::unique_ptr<cutefish::ui::TestDriver> m_driver;
};
