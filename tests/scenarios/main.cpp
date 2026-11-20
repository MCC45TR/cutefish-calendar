// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "ui/glyphs.h"

#include <ui/application.h>

#include <gtest/gtest.h>

using namespace cutefish::ui;

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    Application application(argc, argv, Application::Platform::Headless);
    registerCalendarGlyphs();
    return RUN_ALL_TESTS();
}
