/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogTests.h"
#include <clocale>
#include <iostream>
#include <sstream>

#define LEVEL_PRINT "%z\057"
#define LEVEL_ERR "%z\061"
#define LEVEL_INFO "%z\063"

QString sanitizeBuffer(const std::stringstream &in)
{
  static QRegularExpression timestampRegex("\\[\\S+\\] ");
  QString rtn = QString::fromStdString(in.str()).simplified();
  rtn.remove(timestampRegex);
  return rtn;
}

void LogTests::initTestCase()
{
  std::setlocale(LC_NUMERIC, "C");
  m_log.setFilter(LogLevel::Level::Debug);
}

void LogTests::printWithErrorValidOutput()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message");
}

void LogTests::printTestPrintLevel()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_PRINT "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "test message");
}

void LogTests::printTestWithArgs()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_INFO "test %s", "IamARG");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test IamARG");
}

void LogTests::printTestLogString()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  auto longString = QString(10000, 'a');
  m_log.print(nullptr, 0, LEVEL_INFO "%s", qPrintable(longString));

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString("INFO: %1").arg(longString));
}

void LogTests::printLevelToHigh()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(CLOG_VERBOSE "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString{});
}

void LogTests::printInfoWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_INFO "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test message test file:123");
}

void LogTests::printErrWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message test file:123");
}

void LogTests::printBufferBoundary_data()
{
  QTest::addColumn<int>("length");
  QTest::newRow("fits-with-nul") << 1023;
  QTest::newRow("needs-nul-byte") << 1024;
  QTest::newRow("needs-larger-buffer") << 1025;
}

void LogTests::printBufferBoundary()
{
  QFETCH(int, length);
  const std::string message(static_cast<size_t>(length), 'x');
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());
  m_log.print(nullptr, 0, LEVEL_PRINT "%s", message.c_str());
  std::cout.rdbuf(old);
  QCOMPARE(buffer.str(), message);
}

void LogTests::printWideEncodingError()
{
  const std::string previousLocale = std::setlocale(LC_CTYPE, nullptr);
  std::setlocale(LC_CTYPE, "C");
  bool caught = false;
  std::string error;
  try {
    // Same narrow printf + wide non-ASCII input as the original watchdog INFO log.
    m_log.print(nullptr, 0, LEVEL_INFO "running command: %ls", L"C:\\Users\\\uD608\uC561\uAC80\uC0AC");
  } catch (const std::runtime_error &e) {
    caught = true;
    error = e.what();
  }
  std::setlocale(LC_CTYPE, previousLocale.c_str());
  QVERIFY(caught);
  QVERIFY(error.find("log formatting failed") != std::string::npos);
  QVERIFY(error.find("errno=") != std::string::npos);
}

void LogTests::printUtf8Command()
{
  const auto command = QString::fromWCharArray(L"C:\\Users\\\uD608\uC561\uAC80\uC0AC").toUtf8();
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());
  m_log.print(nullptr, 0, LEVEL_PRINT "%s", command.constData());
  std::cout.rdbuf(old);
  QCOMPARE(QByteArray::fromStdString(buffer.str()), command);
}

QTEST_MAIN(LogTests)
