// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "calendarmodel.h"
#include "calendarstore.h"
#include "holidayfeeds.h"
#include "ui/calendarwindow.h"
#include "ui/dialogs.h"
#include "ui/eventdialog.h"
#include "ui/glyphs.h"
#include "ui/minimonth.h"

#include <base/event_loop.h>
#include <ui/application.h>
#include <ui/combo_box.h>
#include <ui/drag.h>
#include <ui/keys.h>
#include <ui/label.h>
#include <ui/scroll_view.h>
#include <ui/test_input.h>
#include <ui/theme.h>
#include <ui/view.h>
#include <ui/window.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

namespace fs = std::filesystem;
using namespace cutefish::ui;

static uint64_t g_frameTime = 1000000000;

// Frames at 60 Hz, as many as the time asks for, so animations run.
static void advance(Window& window, int milliseconds = 0)
{
    const uint64_t end = g_frameTime + uint64_t(milliseconds) * 1000000;
    do {
        g_frameTime += 16666667;
        TestInput::presentFrame(window, g_frameTime);
    } while (g_frameTime < end);
}

// Runs what the event loop has queued, such as moves done after a drag.
static void runPosted()
{
    cutefish::base::EventLoop& loop = Application::instance()->loop();
    loop.post([&loop] { loop.quit(); });
    loop.run();
}

static void collect(View* view, const std::function<bool(View*)>& match, std::vector<View*>* out)
{
    if (!view->isVisible())
        return;
    if (match(view))
        out->push_back(view);
    for (const auto& child : view->children())
        collect(child.get(), match, out);
}

static std::vector<View*> find(Window* window, const std::string& name)
{
    std::vector<View*> views;
    collect(window->contentView(), [&name](View* view) { return view->name() == name; }, &views);
    return views;
}

template<class T>
static T* one(Window* window, const std::string& name)
{
    const std::vector<View*> views = find(window, name);
    return views.size() == 1 ? dynamic_cast<T*>(views.front()) : nullptr;
}

template<class T>
static T* firstOfType(Window* window)
{
    std::vector<View*> views;
    collect(window->contentView(), [](View* view) { return dynamic_cast<T*>(view) != nullptr; }, &views);
    return views.empty() ? nullptr : static_cast<T*>(views.front());
}

// Scrolls the hour grid so the hour is at its top.
static void showHour(Window* window, int hour)
{
    for (View* grid : find(window, "day:hours"))
        static_cast<ScrollView*>(grid)->setScrollPosition({0, hour * 48.0});
    advance(*window);
}

static PointF centreOf(const View* view)
{
    return view->mapToWindow({view->geometry().width / 2, view->geometry().height / 2});
}

static void activate(ComboBox* box, int index)
{
    box->setCurrentIndex(index);
    if (box->onActivated)
        box->onActivated(index);
}

static const char HolidayEvents[] = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:Holidays\r\n"
                                  "BEGIN:VEVENT\r\nUID:national-day\r\nDTSTART;VALUE=DATE:20261001\r\n"
                                  "DTEND;VALUE=DATE:20261008\r\nSUMMARY:National Day\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

namespace {

class LayoutTest : public testing::Test
{
protected:
    void SetUp() override
    {
        std::string pattern = (fs::temp_directory_path() / "calendar-layout-XXXXXX").string();
        m_dir = mkdtemp(pattern.data());
        m_store = std::make_unique<CalendarStore>(m_dir + "/calendars");
        m_model = std::make_unique<CalendarModel>(m_store.get(), m_dir + "/calendar.conf");
        m_store->load();
    }

    void TearDown() override
    {
        m_window.reset();
        m_model.reset();
        m_store.reset();
        Theme::setCurrent(Theme::light());
        std::error_code error;
        fs::remove_all(m_dir, error);
    }

    CalendarWindow& open()
    {
        m_window = std::make_unique<CalendarWindow>(m_model.get(), false);
        m_window->show();
        advance(*m_window->window());
        return *m_window;
    }

    std::string addEvent(const std::string& title, Date day, int startMinute, int minutes)
    {
        const DateTime start(day, Time(startMinute / 60, startMinute % 60));
        EventForm form = m_model->newEventData(start, start.addSecs(minutes * 60), false);
        form.title = title;
        EXPECT_TRUE(m_model->saveEvent(form, EventFile::AllEvents));
        return title;
    }

    // A new timed event on the day, its editor laid out.
    Window* newEvent(Date day, int startHour = 14)
    {
        if (!m_window)
            open();
        const DateTime start(day, Time(startHour, 0));
        m_window->newEvent(start, start.addSecs(3600), false);
        EXPECT_TRUE(m_window->editor());
        Window* dialog = m_window->editor()->window();
        advance(*dialog);
        return dialog;
    }

    std::string m_dir;
    std::unique_ptr<CalendarStore> m_store;
    std::unique_ptr<CalendarModel> m_model;
    std::unique_ptr<CalendarWindow> m_window;
};

// A drag of files from another application.
class FileOffer : public DropOffer
{
public:
    explicit FileOffer(std::string uriList)
        : m_uriList(std::move(uriList))
    {
    }

    const std::vector<std::string>& mimeTypes() const override { return m_types; }
    uint32_t sourceActions() const override { return CopyAction; }
    void read(const std::string&, std::function<void(std::string data)> done) override { done(m_uriList); }

private:
    std::vector<std::string> m_types{"text/uri-list"};
    std::string m_uriList;
};

}

TEST_F(LayoutTest, SidebarFollowsWidth)
{
    CalendarWindow& window = open();
    Window* frame = window.window();
    EXPECT_EQ(find(frame, "sidebar:panel").size(), 1u);

    // A narrow window hides it, and grows by it when it is asked for.
    frame->resize({780, 640});
    advance(*frame, 300);
    EXPECT_TRUE(find(frame, "sidebar:panel").empty());
    EXPECT_TRUE(m_model->sidebarVisible());
    window.toggleSidebar();
    advance(*frame);
    EXPECT_EQ(frame->size().width, 780 + CalendarWindow::SidebarWidth);
    EXPECT_EQ(find(frame, "sidebar:panel").size(), 1u);
    EXPECT_EQ(window.sidebarReveal(), 1.0);
    EXPECT_EQ(find(frame, "calendar:content").front()->geometry().width, 780);
    frame->resize({600, 640});
    advance(*frame, 300);
    window.toggleSidebar();
    advance(*frame);
    EXPECT_EQ(frame->size().width, CalendarWindow::DockWidth);
    EXPECT_EQ(window.sidebarReveal(), 1.0);

    // A narrow window that cannot grow gives it some of the calendar's width,
    // and the search field goes when the header has no room for it.
    WindowStates tiled;
    tiled.tiled = true;
    TestInput::configure(*frame, 640, 640, tiled);
    advance(*frame, 300);
    EXPECT_TRUE(find(frame, "sidebar:panel").empty());
    EXPECT_EQ(find(frame, "header:search").size(), 1u);
    window.toggleSidebar();
    advance(*frame, 300);
    EXPECT_EQ(frame->size().width, 640);
    EXPECT_EQ(find(frame, "sidebar:panel").size(), 1u);
    EXPECT_EQ(find(frame, "calendar:content").front()->geometry().width, 640 - CalendarWindow::SidebarWidth);
    EXPECT_TRUE(find(frame, "header:search").empty());
    window.toggleSidebar();
    advance(*frame, 300);
    EXPECT_TRUE(find(frame, "sidebar:panel").empty());
    EXPECT_TRUE(m_model->sidebarVisible());
    TestInput::configure(*frame, 1100, 740, WindowStates{});
    advance(*frame, 300);
    EXPECT_EQ(window.sidebarReveal(), 1.0);
}

TEST_F(LayoutTest, DraggingOnTheHoursMarksANewEvent)
{
    m_model->setView("day");
    CalendarWindow& window = open();
    Window* frame = window.window();
    const std::vector<View*> grids = find(frame, "day:hours");
    ASSERT_EQ(grids.size(), 1u);
    auto* scroll = static_cast<ScrollView*>(grids.front());
    scroll->setScrollPosition({0, 9 * 48});
    advance(*frame);
    View* canvas = scroll->contentView();

    // From 10:00 to 11:30 on the hour grid, 48 px an hour below a 6 px margin.
    const PointF from = canvas->mapToWindow({300, 6 + 10 * 48 + 2});
    const PointF to = canvas->mapToWindow({300, 6 + 11.5 * 48});
    TestInput::press(*frame, from);
    TestInput::movePointer(*frame, {from.x, (from.y + to.y) / 2});
    TestInput::movePointer(*frame, to);
    advance(*frame);
    EXPECT_EQ(find(frame, "grid:newEvent").size(), 1u);
    TestInput::release(*frame, to);
    ASSERT_TRUE(window.editor());
    EXPECT_EQ(window.editor()->event().start, DateTime(window.currentDate(), Time(10, 0)));
    EXPECT_EQ(window.editor()->event().end, DateTime(window.currentDate(), Time(11, 30)));
}

TEST_F(LayoutTest, ANewEventMarkedToTheLastHourEndsAtMidnight)
{
    m_model->setView("day");
    CalendarWindow& window = open();
    Window* frame = window.window();
    showHour(frame, 20);
    View* canvas = static_cast<ScrollView*>(find(frame, "day:hours").front())->contentView();
    const Date next = window.currentDate().addDays(1);

    const PointF from = canvas->mapToWindow({300, 6 + 23 * 48 + 2});
    const PointF to = canvas->mapToWindow({300, 6 + 24 * 48 + 4});
    TestInput::press(*frame, from);
    TestInput::movePointer(*frame, {from.x, from.y + 30});
    TestInput::movePointer(*frame, to);
    TestInput::release(*frame, to);
    ASSERT_TRUE(window.editor());
    EXPECT_EQ(window.editor()->event().start, DateTime(window.currentDate(), Time(23, 0)));
    EXPECT_EQ(window.editor()->event().end, DateTime(next, Time(0, 0)));
    window.editor()->close();
    runPosted();

    // A double click in the last hour gives it an hour too.
    TestInput::click(*frame, canvas->mapToWindow({300, 6 + 23.5 * 48 + 10}));
    TestInput::click(*frame, canvas->mapToWindow({300, 6 + 23.5 * 48 + 10}));
    ASSERT_TRUE(window.editor());
    EXPECT_EQ(window.editor()->event().start, DateTime(window.currentDate(), Time(23, 0)));
    EXPECT_EQ(window.editor()->event().end, DateTime(next, Time(0, 0)));
}

TEST_F(LayoutTest, DraggingABlockMovesTheEvent)
{
    addEvent("Review", Date::currentDate(), 15 * 60, 60);
    m_model->setView("day");
    CalendarWindow& window = open();
    Window* frame = window.window();
    showHour(frame, 14);
    View* block = find(frame, "event:block").front();
    const PointF start = centreOf(block);
    TestInput::press(*frame, start);
    TestInput::movePointer(*frame, {start.x, start.y + 10});
    TestInput::movePointer(*frame, {start.x, start.y + 48});
    TestInput::release(*frame, {start.x, start.y + 48});
    runPosted();
    const std::vector<Occurrence> today = m_store->occurrences(Date::currentDate(), Date::currentDate().addDays(1));
    ASSERT_EQ(today.size(), 1u);
    EXPECT_EQ(today.front().start, DateTime(Date::currentDate(), Time(16, 0)));
}

TEST_F(LayoutTest, MiniMonthAndYearViewMoveTheDay)
{
    m_model->setView("year");
    CalendarWindow& window = open();
    Window* frame = window.window();
    // A double click on a day of the year opens the day.
    const std::vector<View*> years = find(frame, "view:year");
    ASSERT_EQ(years.size(), 1u);
    window.setCurrentDate(Date(window.currentDate().year(), 3, 1));
    window.showMonth(Date(window.currentDate().year(), 6, 15));
    EXPECT_EQ(window.view(), "month");
    EXPECT_EQ(window.currentDate(), Date(window.currentDate().year(), 6, 15));

    // The sidebar's month follows, and a click there selects the day.
    const std::vector<View*> cards = find(frame, "sidebar:month");
    ASSERT_EQ(cards.size(), 1u);
    advance(*frame);
    window.setCurrentDate(m_model->today());
    advance(*frame);
    View* days = cards.front()->children().front()->children().back().get();
    // The last row's middle cell lies in the next month or late in this one.
    TestInput::click(*frame, days->mapToWindow({days->geometry().width / 2, days->geometry().height - 5}));
    EXPECT_NE(window.currentDate(), m_model->today());
}

// What the window answers the compositor over a point of the calendar.
TEST_F(LayoutTest, DropsTakeOnlyCalendarFilesAsCopies)
{
    CalendarWindow& window = open();
    Window* frame = window.window();
    FileOffer other("file:///tmp/notes.txt\r\n");
    EXPECT_EQ(TestInput::dragEnter(*frame, {600, 400}, &other), uint32_t(NoAction));
    EXPECT_EQ(TestInput::drop(*frame), uint32_t(NoAction));
    runPosted();

    FileOffer files("# dropped\r\nfile://" + m_dir + "/trip.ics\r\n");
    EXPECT_EQ(TestInput::dragEnter(*frame, {600, 400}, &files), uint32_t(CopyAction));
    EXPECT_EQ(TestInput::drop(*frame), uint32_t(CopyAction));
    runPosted();
}

TEST_F(LayoutTest, SubscriptionsSitUnderTheCalendars)
{
    const std::string id = m_store->createSubscription("Holidays", Rgb{0xff, 0, 0},
                                                       {.url = "https://example.com/holidays.ics"});
    ASSERT_EQ(m_store->replaceEvents(id, EventFile::split(HolidayEvents)), 1);
    CalendarWindow& window = open();
    runPosted();
    advance(*window.window());

    View* calendars = one<View>(window.window(), "sidebar:calendars");
    View* heading = one<View>(window.window(), "sidebar:subscriptionsHeading");
    View* subscriptions = one<View>(window.window(), "sidebar:subscriptions");
    ASSERT_TRUE(calendars && heading && subscriptions);
    const double calendarsBottom = calendars->mapToWindow({0, calendars->geometry().height}).y;
    const double headingTop = heading->parent()->mapToWindow({0, 0}).y;
    const double headingBottom = heading->parent()->mapToWindow({0, heading->parent()->geometry().height}).y;
    EXPECT_EQ(headingTop - calendarsBottom, 12);
    EXPECT_EQ(subscriptions->mapToWindow({0, 0}).y - headingBottom, 12);
    EXPECT_EQ(subscriptions->children().front()->children().size(), 1u);
}

TEST_F(LayoutTest, EditorLaysOutItsCards)
{
    Window* dialog = newEvent(Date(2026, 10, 14));
    // Cards and spacings as the Qt dialog: whole pixels, 12 apart.
    View* content = find(dialog, "editor:content").front();
    std::vector<RectF> rects;
    for (const auto& child : content->children()) {
        if (child->isVisible())
            rects.push_back(child->geometry());
    }
    ASSERT_EQ(rects.size(), 5u);
    // Title and location: two fields of 44 and a separator of one device pixel.
    EXPECT_EQ(rects[0].y, 0);
    EXPECT_EQ(rects[0].height, 89);
    EXPECT_EQ(rects[1].y, 101);
    EXPECT_EQ(rects[1].height, 132);
    EXPECT_EQ(rects[2].y, 245);
    EXPECT_EQ(rects[2].height, 132);
    EXPECT_EQ(rects[3].y, 389);
    EXPECT_EQ(rects[3].height, 84);
    EXPECT_EQ(rects[4].y, 489);
}

TEST_F(LayoutTest, TheDatePopupPicksADayAndKeepsTheLength)
{
    const Date day(2026, 10, 14);
    Window* dialog = newEvent(day, 23);
    auto* start = one<DateButton>(dialog, "editor:startDate");
    auto* end = one<DateButton>(dialog, "editor:endDate");
    ASSERT_TRUE(start && end);
    // From 23:00 to midnight: the end is on the next day.
    EXPECT_EQ(end->date(), day.addDays(1));

    TestInput::click(*dialog, centreOf(start));
    Window* popup = start->popupWindow();
    ASSERT_TRUE(popup);
    advance(*popup);
    EXPECT_EQ(popup->contentView()->geometry().width, 252);
    auto* month = firstOfType<MiniMonth>(popup);
    ASSERT_TRUE(month);
    EXPECT_EQ(month->month(), Date(2026, 10, 1));
    const int index = int(month->gridStart().daysTo(Date(2026, 10, 20)));
    const double cell = month->geometry().width / 7;
    TestInput::click(*popup, month->mapToWindow({(index % 7 + 0.5) * cell, 28 + (index / 7 + 0.5) * 28}));
    runPosted();
    EXPECT_FALSE(start->isPopupOpen());
    EXPECT_EQ(start->date(), Date(2026, 10, 20));
    EXPECT_EQ(end->date(), Date(2026, 10, 21));
}

TEST_F(LayoutTest, DialogsKeepTheirWidths)
{
    // The question for a repeating event's new rule.
    const Date day = Date::currentDate();
    const DateTime start(day, Time(9, 0));
    EventForm form = m_model->newEventData(start, start.addSecs(1800), false);
    form.title = "Standup";
    form.repeat = Repeat::Daily;
    ASSERT_TRUE(m_model->saveEvent(form, EventFile::AllEvents));
    const std::vector<Occurrence> found = m_store->occurrences(day.addDays(1), day.addDays(2));
    ASSERT_EQ(found.size(), 1u);
    CalendarWindow& window = open();
    window.editEvent(found.front().key, found.front().recurrenceKey);
    ASSERT_TRUE(window.editor());
    Window* editor = window.editor()->window();
    advance(*editor);
    activate(one<ComboBox>(editor, "editor:repeat"), 3);
    TestInput::typeKey(*editor, key::Return, ControlModifier);
    runPosted();
    ScopeDialog* scope = window.scopeDialog();
    ASSERT_TRUE(scope);
    advance(*scope->window());
    EXPECT_EQ(scope->window()->size().width, 360);
    scope->close();
    window.editor()->close();
    runPosted();

    window.editCalendar(m_model->calendars().front());
    ASSERT_TRUE(window.calendarDialog());
    advance(*window.calendarDialog()->window());
    EXPECT_EQ(window.calendarDialog()->window()->size().width, 360);
    window.calendarDialog()->close();
    runPosted();

    window.importFiles({"file:///tmp/a.ics", "file:///tmp/b.ics"});
    ASSERT_TRUE(window.importDialog());
    advance(*window.importDialog()->window());
    EXPECT_EQ(window.importDialog()->window()->size().width, 380);
    window.importDialog()->close();
    runPosted();

    window.newSubscription();
    ASSERT_TRUE(window.subscriptionDialog());
    advance(*window.subscriptionDialog()->window());
    EXPECT_EQ(window.subscriptionDialog()->window()->size().width, 380);
    window.subscriptionDialog()->close();
    runPosted();

    SettingsDialog settings(window.window(), m_model.get());
    settings.show();
    advance(*settings.window());
    EXPECT_EQ(settings.window()->size().width, 420);
}

TEST_F(LayoutTest, TheHolidayListKeepsOneGapToItsCard)
{
    const std::vector<HolidayFeed> feeds = HolidayFeeds::list();
    ASSERT_GE(feeds.size(), 2u);
    CalendarWindow& window = open();
    window.newSubscription();
    SubscriptionDialog* dialog = window.subscriptionDialog();
    ASSERT_TRUE(dialog);
    Window* frame = dialog->window();
    advance(*frame);
    const double closedHeight = frame->size().height;

    TestInput::click(*frame, centreOf(one<View>(frame, "subscription:holidaysToggle")));
    advance(*frame);
    advance(*frame);
    View* list = one<View>(frame, "subscription:holidays");
    ASSERT_TRUE(list);
    EXPECT_EQ(list->geometry().height, 6 * 34 + 12);
    EXPECT_EQ(frame->size().height, closedHeight + 16 + list->geometry().height);
    View* row = one<View>(frame, "holidays:" + feeds[0].url);
    ASSERT_TRUE(row);
    // One gap of 6 to the card's edges.
    const PointF card = list->mapToWindow({0, 0});
    const PointF first = row->mapToWindow({0, 0});
    EXPECT_EQ(first.x - card.x, 6);
    EXPECT_EQ(first.y - card.y, 6);
    EXPECT_EQ(list->geometry().width - row->geometry().width, 12);
}

static void save(Window* window, double scale, const std::string& file)
{
    window->setScale(scale);
    advance(*window);
    if (!window->grab().savePng(file + ".png"))
        std::fprintf(stderr, "cannot write %s.png\n", file.c_str());
    std::ofstream(file + ".txt") << window->layoutDump();
}

// Renders the dialogs over a calendar directory, for comparing with the live
// application: calendar-layout-tests --capture ROOT OUTDIR [SCALE].
static int capture(const std::string& root, const std::string& out, double scale)
{
    fs::create_directories(out);
    const std::string settings = out + "/calendar.conf";
    std::ofstream(settings) << "[General]\ndefaultCalendar=personal\nsidebarVisible=true\nview=month\n";
    CalendarStore store(root);
    CalendarModel model(&store, settings);
    store.load();
    const Date day(2026, 10, 14);
    for (const auto& [name, theme] : {std::pair{"light", Theme::light()}, std::pair{"dark", Theme::dark()}}) {
        Theme::setCurrent(theme);
        const std::string prefix = out + "/" + name + "-";
        CalendarWindow window(&model, false);
        window.window()->setScale(scale);
        window.show();
        advance(*window.window());

        window.newEvent(DateTime(day, Time(9, 0)), DateTime(day, Time(10, 0)), false);
        Window* editor = window.editor()->window();
        save(editor, scale, prefix + "new-event");
        auto* start = one<DateButton>(editor, "editor:startDate");
        TestInput::click(*editor, centreOf(start));
        if (Window* popup = start->popupWindow())
            save(popup, scale, prefix + "date-popup");
        start->dismiss();
        runPosted();
        activate(one<ComboBox>(editor, "editor:repeat"), 1);
        activate(one<ComboBox>(editor, "editor:repeatEnd"), 2);
        one<ComboBox>(editor, "editor:endTime")->setEditText("8:00 am");
        one<ComboBox>(editor, "editor:endTime")->onAccepted();
        save(editor, scale, prefix + "repeat-after");
        activate(one<ComboBox>(editor, "editor:repeatEnd"), 1);
        save(editor, scale, prefix + "repeat-until");
        window.editor()->close();
        runPosted();

        for (const Occurrence& occurrence : store.occurrences(day, day.addDays(1))) {
            if (occurrence.recurring) {
                window.deleteEvent(occurrence.key, occurrence.recurrenceKey);
                break;
            }
        }
        if (ScopeDialog* scope = window.scopeDialog()) {
            save(scope->window(), scale, prefix + "scope-delete");
            scope->close();
        }
        window.newCalendar();
        save(window.calendarDialog()->window(), scale, prefix + "new-calendar");
        window.calendarDialog()->close();
        window.importFiles({"file:///tmp/Holidays%202026.ics"});
        save(window.importDialog()->window(), scale, prefix + "import");
        window.importDialog()->close();
        SettingsDialog settingsDialog(window.window(), &model);
        settingsDialog.show();
        save(settingsDialog.window(), scale, prefix + "settings");
        runPosted();
    }
    return 0;
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    Application application(argc, argv, Application::Platform::Headless);
    registerCalendarGlyphs();
    if (argc >= 4 && std::string(argv[1]) == "--capture")
        return capture(argv[2], argv[3], argc > 4 ? std::stod(argv[4]) : 1.5);
    return RUN_ALL_TESTS();
}
