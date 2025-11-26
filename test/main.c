//
//  main.c
//  test suite for sqlitemcp extension
//
//  Created by Gioele Cantoni on 05/11/25.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define TEST_PASSED "\033[0;32m[PASS]\033[0m"
#define TEST_FAILED "\033[0;31m[FAIL]\033[0m"

static int test_count = 0;
static int passed_count = 0;
static int failed_count = 0;
static int skipped_count = 0;

void run_test(const char *test_name, int (*test_func)(sqlite3 *db)) {
    sqlite3 *db;
    int rc = sqlite3_open(":memory:", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s %s: Failed to open database\n", TEST_FAILED, test_name);
        failed_count++;
        test_count++;
        return;
    }

    rc = sqlite3_enable_load_extension(db, 1);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s %s: Failed to enable extension loading\n", TEST_FAILED, test_name);
        sqlite3_close(db);
        failed_count++;
        test_count++;
        return;
    }
    rc = sqlite3_load_extension(db, "./dist/mcp", 0, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s %s: Failed to load extension: %s\n",
                TEST_FAILED, test_name, sqlite3_errmsg(db));
        sqlite3_close(db);
        failed_count++;
        test_count++;
        return;
    }

    // Run the test
    int result = test_func(db);

    if (result == 0) {
        printf("%s %s\n", TEST_PASSED, test_name);
        passed_count++;
    } else {
        printf("%s %s\n", TEST_FAILED, test_name);
        failed_count++;
    }

    test_count++;
    sqlite3_close(db);
}

// Test: Check if extension loads successfully
int test_extension_loads(sqlite3 *db) {
    return 0; // If we got here, the extension loaded
}

// Test: mcp_version() returns a valid version string
int test_mcp_version(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT mcp_version()", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const unsigned char *version = sqlite3_column_text(stmt, 0);
    if (!version || strlen((const char *)version) == 0) {
        fprintf(stderr, "    Version string is empty\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    // Check if version matches expected format (e.g., "0.1.0")
    if (strcmp((const char *)version, "0.1.0") != 0) {
        fprintf(stderr, "    Unexpected version: %s\n", version);
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

// Test: mcp_connect() to Playwright MCP server
int test_mcp_connect(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8931/sse', 1)",
        -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const unsigned char *result = sqlite3_column_text(stmt, 0);
    if (!result) {
        fprintf(stderr, "    Result is NULL\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    // Check if connection was successful
    if (strstr((const char *)result, "\"status\"") == NULL ||
        strstr((const char *)result, "connected") == NULL) {
        fprintf(stderr, "    Connection failed: %s\n", result);
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("    Connected: %s\n", result);
    sqlite3_finalize(stmt);
    return 0;
}

// Test: mcp_list_tools() after connecting
int test_mcp_list_tools(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    // First connect
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8931/sse', 1)",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare connect: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to connect: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);

    // Now list tools
    rc = sqlite3_prepare_v2(db, "SELECT mcp_list_tools()", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare list_tools: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute list_tools: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const unsigned char *result = sqlite3_column_text(stmt, 0);
    if (!result) {
        fprintf(stderr, "    Result is NULL\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    // Check if tools list contains expected structure
    if (strstr((const char *)result, "\"tools\"") == NULL) {
        fprintf(stderr, "    Unexpected tools result: %s\n", result);
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("    Tools listed successfully\n");

    sqlite3_finalize(stmt);
    return 0;
}

// Test: mcp_call_tool() to navigate with Playwright
int test_mcp_call_tool(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    // First connect
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8931/sse', 1)",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare connect: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to connect: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);

    // Now call playwright_navigate tool
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_call_tool('playwright_navigate', '{\"url\": \"https://example.com\"}')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare call_tool: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute call_tool: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const unsigned char *result = sqlite3_column_text(stmt, 0);
    if (!result) {
        fprintf(stderr, "    Result is NULL\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    // Check if tool call returned a result
    if (strstr((const char *)result, "\"result\"") == NULL) {
        fprintf(stderr, "    Unexpected tool call result: %s\n", result);
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("    Tool called successfully\n");
    sqlite3_finalize(stmt);
    return 0;
}

// Test: Navigate to sqlite.ai and get page title
int test_mcp_browser(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;
    const unsigned char *result;

    printf("    Navigating to sqlite.ai to get page title...\n");

    // Step 1: Connect to Playwright MCP server
    printf("    [1/4] Connecting to Playwright MCP server...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8931/sse', 1)",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare connect: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to connect: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    result = sqlite3_column_text(stmt, 0);
    if (!result || strstr((const char *)result, "connected") == NULL) {
        fprintf(stderr, "    Connection failed: %s\n", result ? (const char *)result : "NULL");
        sqlite3_finalize(stmt);
        return 1;
    }
    printf("    ✓ Connected to Playwright server\n");
    sqlite3_finalize(stmt);

    // Step 2: Navigate to sqlite.ai
    printf("    [2/4] Navigating to sqlite.ai...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_call_tool('browser_navigate', '{\"url\": \"https://sqlite.ai\"}')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare navigate: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to navigate: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    printf("    ✓ Navigated to sqlite.ai\n");
    sqlite3_finalize(stmt);

    // Step 3: Wait for page to load
    printf("    [3/4] Waiting for page to load...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_call_tool('browser_wait_for', '{\"time\": 2}')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare wait: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to wait: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);

    // Step 4: Extract page title
    printf("    [4/4] Extracting page title...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_call_tool('browser_evaluate', '{\"function\": \"() => document.title\"}')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare evaluate: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to evaluate: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    result = sqlite3_column_text(stmt, 0);
    if (!result) {
        fprintf(stderr, "    Results are NULL\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    // Extract the page title from the JSON response
    // Look for "### Result\n\"" and extract the title between quotes
    const char *title_marker = strstr((const char *)result, "### Result\\n\\\"");
    char page_title[256] = {0};

    if (title_marker) {
        title_marker += strlen("### Result\\n\\\"");
        const char *end_quote = strstr(title_marker, "\\\"");
        if (end_quote) {
            size_t len = end_quote - title_marker;
            if (len > 0 && len < sizeof(page_title)) {
                strncpy(page_title, title_marker, len);
                page_title[len] = '\0';
            }
        }
    }

    if (strlen(page_title) == 0) {
        fprintf(stderr, "    ❌ Failed to extract page title\n");
        fprintf(stderr, "    Raw response: %s\n", (const char *)result);
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("\n    === Page Title from sqlite.ai ===\n");
    printf("    %s\n", page_title);
    printf("    ==================================\n\n");

    // Validate the page title matches expected value
    const char *expected_title = "SQLite AI - Smart Edge Databases with Cloud Sync and Intelligence";
    if (strcmp(page_title, expected_title) != 0) {
        fprintf(stderr, "    ❌ Page title does not match expected value\n");
        fprintf(stderr, "    Expected: %s\n", expected_title);
        fprintf(stderr, "    Got: %s\n", page_title);
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("    ✓ Page title matches expected value\n");

    sqlite3_finalize(stmt);
    return 0;
}


int main(void) {
    printf("\n=== sqlite-mcp Test Suite ===\n\n");

    // Basic tests
    run_test("Extension loads successfully", test_extension_loads);
    run_test("mcp_version() returns correct version", test_mcp_version);

    // MCP Playwright server tests (requires server running on localhost:8931)
    printf("\n--- MCP Server Tests ---\n");
    printf("Note: These tests require Playwright MCP server running on localhost:8931\n");
    printf("      Start server with: npx @playwright/mcp@latest --port 8931\n\n");
    run_test("mcp_connect() to Playwright server", test_mcp_connect);
    run_test("mcp_list_tools() after connecting", test_mcp_list_tools);
    run_test("mcp_call_tool() navigate example.com", test_mcp_call_tool);

    printf("\n--- sqlite.ai Page Title Demo ---\n");
    run_test("Navigate to sqlite.ai and get page title", test_mcp_browser);

    printf("\n=== Test Results ===\n");
    printf("Total:  %d\n", test_count);
    printf("Passed: %d\n", passed_count);
    printf("Failed: %d\n", failed_count);
    if (skipped_count > 0) {
        printf("Skipped: %d\n", skipped_count);
    }

    return (failed_count == 0) ? 0 : 1;
}
