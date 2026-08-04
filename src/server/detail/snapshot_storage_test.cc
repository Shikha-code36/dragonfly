// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.

#include "server/detail/snapshot_storage.h"

#include <gtest/gtest.h>

namespace dfly::detail {

using namespace std;

// FindMatchingFile and SnapStat are `protected` - expose them for testing. Never instantiated:
// FindMatchingFile is static, so calling it doesn't require an object of this (abstract) class.
struct OpenSnapshotStorage : SnapshotStorage {
  using SnapshotStorage::FindMatchingFile;
  using SnapshotStorage::SnapStat;
};

namespace {

string Match(string_view prefix, string_view dbfilename,
            vector<pair<string, int64_t>> raw_keys) {
  vector<OpenSnapshotStorage::SnapStat> keys;
  keys.reserve(raw_keys.size());
  for (auto& [name, ts] : raw_keys) {
    keys.emplace_back(std::move(name), ts);
  }
  return OpenSnapshotStorage::FindMatchingFile(prefix, dbfilename, std::move(keys));
}

TEST(FindMatchingFileTest, ExactNameWithExtension) {
  EXPECT_EQ(Match("", "dump.rdb", {{"dump.rdb", 1}}), "dump.rdb");
  EXPECT_EQ(Match("", "dump.rdb", {{"other.rdb", 1}}), "");
}

TEST(FindMatchingFileTest, NoKeysReturnsEmpty) {
  EXPECT_EQ(Match("", "dump.rdb", {}), "");
}

TEST(FindMatchingFileTest, LiteralDotIsNotAWildcard) {
  // Regression check: the old std::regex-based matcher escaped dots everywhere except in the
  // appended "(-summary.dfs|.rdb)" extension alternation, where an unescaped '.' acted as a
  // regex wildcard. The iterative matcher treats it as a literal character everywhere.
  EXPECT_EQ(Match("", "dump.rdb", {{"dumpXrdb", 1}}), "");
  EXPECT_EQ(Match("", "dump.rdb", {{"dump.rdb", 1}}), "dump.rdb");
}

TEST(FindMatchingFileTest, NoExtensionAllowsRdbOrSummarySuffix) {
  EXPECT_EQ(Match("", "mydb", {{"mydb.rdb", 1}}), "mydb.rdb");
  EXPECT_EQ(Match("", "mydb", {{"mydb-summary.dfs", 1}}), "mydb-summary.dfs");
  EXPECT_EQ(Match("", "mydb", {{"mydb.rd", 1}}), "");
  EXPECT_EQ(Match("", "mydb", {{"mydb.rdbx", 1}}), "");
  EXPECT_EQ(Match("", "mydb", {{"mydbextra.rdb", 1}}), "");
}

TEST(FindMatchingFileTest, TimestampPlaceholder) {
  EXPECT_EQ(Match("", "dump-{timestamp}.rdb", {{"dump-2024-01-24T11:18:09.rdb", 1}}),
            "dump-2024-01-24T11:18:09.rdb");
  // Wrong shape: missing a digit in the year.
  EXPECT_EQ(Match("", "dump-{timestamp}.rdb", {{"dump-204-01-24T11:18:09.rdb", 1}}), "");
  // Wrong shape: non-digit where a digit is required.
  EXPECT_EQ(Match("", "dump-{timestamp}.rdb", {{"dump-2024-01-2XT11:18:09.rdb", 1}}), "");
  // Wrong separators.
  EXPECT_EQ(Match("", "dump-{timestamp}.rdb", {{"dump-2024/01/24T11:18:09.rdb", 1}}), "");
}

TEST(FindMatchingFileTest, YearMonthDayPlaceholders) {
  EXPECT_EQ(Match("", "dump-{Y}{m}{d}.rdb", {{"dump-20240124.rdb", 1}}), "dump-20240124.rdb");
  EXPECT_EQ(Match("", "dump-{Y}{m}{d}.rdb", {{"dump-2024012.rdb", 1}}), "");    // too short
  EXPECT_EQ(Match("", "dump-{Y}{m}{d}.rdb", {{"dump-2024012X.rdb", 1}}), "");  // non-digit
}

TEST(FindMatchingFileTest, PicksMostRecentAmongMultipleMatches) {
  EXPECT_EQ(Match("", "dump-{timestamp}.rdb",
                  {{"dump-2024-01-24T11:18:09.rdb", 1}, {"dump-2024-06-01T00:00:00.rdb", 2}}),
            "dump-2024-06-01T00:00:00.rdb");
}

TEST(FindMatchingFileTest, PrefixIsMatchedLiterally) {
  EXPECT_EQ(Match("snapshots/", "dump.rdb", {{"snapshots/dump.rdb", 1}}), "snapshots/dump.rdb");
  EXPECT_EQ(Match("snapshots/", "dump.rdb", {{"other/dump.rdb", 1}}), "");
}

}  // namespace
}  // namespace dfly::detail
