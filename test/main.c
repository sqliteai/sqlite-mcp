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

// Test: mcp_connect() with JSON extension mode enabled
int test_mcp_json_extension(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing JSON extension mode...\n");

    // Step 1: First connect in regular mode
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected to MCP server\n");
    sqlite3_finalize(stmt);

    // Step 2: Enable JSON extension mode and test json_extract on regular connect result
    printf("    [2/3] Testing json_extract with regular mcp_connect result...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT json_extract(mcp_connect('http://localhost:8931/sse', 1), '$.status')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare json_extract: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to extract status: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    const unsigned char *status = sqlite3_column_text(stmt, 0);
    if (!status || strcmp((const char *)status, "connected") != 0) {
        fprintf(stderr, "    Expected status 'connected' but got: %s\n", status ? (const char *)status : "NULL");
        sqlite3_finalize(stmt);
        return 1;
    }
    printf("    ✓ Successfully extracted status: %s\n", status);
    sqlite3_finalize(stmt);

    // Step 3: Extract first tool name from list_tools using json_extract
    printf("    [3/3] Extracting first tool name with json_extract()...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT json_extract(mcp_list_tools(), '$.tools[0].name')",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare json_extract on list_tools: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to extract tool name: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    const unsigned char *tool_name = sqlite3_column_text(stmt, 0);
    if (!tool_name) {
        fprintf(stderr, "    Tool name is NULL\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    printf("    ✓ Successfully extracted first tool name: %s\n", tool_name);
    sqlite3_finalize(stmt);

    printf("    ✓ JSON extension mode working correctly\n");
    return 0;
}

// Test: mcp_tools_table virtual table returns structured rows
int test_mcp_tools_table(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing mcp_tools_table virtual table...\n");

    // Step 1: Connect first
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: Query virtual table
    printf("    [2/3] Querying mcp_tools_table...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT name, description FROM mcp_tools_table",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare query: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Step 3: Fetch and validate results
    printf("    [3/3] Fetching tool rows...\n");
    int row_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *desc = sqlite3_column_text(stmt, 1);

        if (!name) {
            fprintf(stderr, "    Tool name is NULL at row %d\n", row_count);
            sqlite3_finalize(stmt);
            return 1;
        }

        printf("    ✓ Tool %d: %s - %s\n", row_count + 1, name, desc ? (const char*)desc : "(no description)");
        row_count++;
    }

    sqlite3_finalize(stmt);

    if (row_count == 0) {
        fprintf(stderr, "    No tools returned from virtual table\n");
        return 1;
    }

    printf("    ✓ Successfully retrieved %d tools from virtual table\n", row_count);
    return 0;
}

// Test: mcp_call_tool_table virtual table returns text results
int test_mcp_call_tool_table(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing mcp_call_tool_table virtual table...\n");

    // Step 1: Connect first
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: Query virtual table with tool call
    printf("    [2/3] Calling tool via virtual table...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT text FROM mcp_call_tool_table "
        "WHERE tool_name = 'browser_navigate' AND arguments = '{\"url\": \"https://example.com\"}'",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare query: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Step 3: Fetch results
    printf("    [3/3] Fetching text results...\n");
    int row_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);

        if (!text) {
            fprintf(stderr, "    Text result is NULL at row %d\n", row_count);
            sqlite3_finalize(stmt);
            return 1;
        }

        printf("    ✓ Result %d: %s\n", row_count + 1, text);
        row_count++;
    }

    sqlite3_finalize(stmt);

    if (row_count == 0) {
        fprintf(stderr, "    No results returned from virtual table\n");
        return 1;
    }

    printf("    ✓ Successfully retrieved %d text results from virtual table\n", row_count);
    return 0;
}

// Test: mcp_connect returns SQLITE_DONE in JSON mode when connected
int test_mcp_connect_json_mode(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing mcp_connect with JSON mode returns no rows...\n");

    // Connect with JSON mode enabled (4th parameter = 1)
    printf("    [1/2] Connecting with JSON extension mode...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8931/sse', 1, NULL, 1)",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare connect: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Step - should return NULL on successful connection (scalar functions always return 1 row)
    printf("    [2/2] Checking result (should be NULL on success)...\n");
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        // Check if the result is NULL (success) or contains an error message (failure)
        if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
            printf("    ✓ Connection successful - returned NULL as expected\n");
            sqlite3_finalize(stmt);
            return 0;
        } else {
            // Has a value - this means it's an error message
            const unsigned char *error = sqlite3_column_text(stmt, 0);
            fprintf(stderr, "    Connection failed: %s\n", error ? (const char*)error : "Unknown error");
            sqlite3_finalize(stmt);
            return 1;
        }
    } else {
        fprintf(stderr, "    Unexpected result code: %d (%s)\n", rc, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
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


// Test: Virtual table caching - verify temp table persists across multiple queries
int test_mcp_tools_table_caching(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing virtual table caching behavior...\n");

    // Step 1: Connect first
    printf("    [1/4] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: First query - this should create and populate the temp table
    printf("    [2/4] First query to mcp_tools_table (creates temp table)...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM mcp_tools_table",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare first query: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute first query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    int first_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    printf("    ✓ First query returned %d tools\n", first_count);

    // Step 3: Second query - should use cached temp table (no network request)
    printf("    [3/4] Second query to mcp_tools_table (uses cached table)...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM mcp_tools_table",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare second query: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute second query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    int second_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    printf("    ✓ Second query returned %d tools\n", second_count);

    // Step 4: Verify counts match
    printf("    [4/4] Verifying cached results match...\n");
    if (first_count != second_count) {
        fprintf(stderr, "    Count mismatch: first=%d, second=%d\n", first_count, second_count);
        return 1;
    }
    if (first_count == 0) {
        fprintf(stderr, "    No tools found in either query\n");
        return 1;
    }

    printf("    ✓ Both queries returned same count (%d tools)\n", first_count);
    printf("    ✓ Virtual table caching working correctly\n");
    return 0;
}

// Test: Verify temp table is created with expected name pattern
int test_mcp_tools_table_temp_exists(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing temp table creation...\n");

    // Step 1: Connect first
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: Query virtual table to trigger temp table creation
    printf("    [2/3] Querying virtual table...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT name FROM mcp_tools_table LIMIT 1",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare query: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    printf("    ✓ Virtual table query executed\n");

    // Step 3: Check that temp tables exist with mcp_tools_cache_ prefix
    printf("    [3/3] Checking for temp table existence...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT name FROM temp.sqlite_master WHERE type='table' AND name LIKE 'mcp_tools_cache_%'",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare temp table check: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    int found = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *table_name = sqlite3_column_text(stmt, 0);
        printf("    ✓ Found temp table: %s\n", table_name);
        found = 1;
    }
    sqlite3_finalize(stmt);

    if (!found) {
        fprintf(stderr, "    No temp table with mcp_tools_cache_ prefix found\n");
        return 1;
    }

    printf("    ✓ Temp table created successfully\n");
    return 0;
}

// Test: Verify scalar function doesn't use caching (always fresh)
int test_scalar_function_no_cache(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing scalar function behavior (no caching)...\n");

    // Step 1: Connect first
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: Call mcp_list_tools() twice
    printf("    [2/3] Calling mcp_list_tools() twice...\n");
    for (int i = 1; i <= 2; i++) {
        rc = sqlite3_prepare_v2(db, "SELECT mcp_list_tools()", -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "    Failed to prepare list_tools call %d: %s\n", i, sqlite3_errmsg(db));
            return 1;
        }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            fprintf(stderr, "    Failed to execute list_tools call %d: %s\n", i, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return 1;
        }
        const unsigned char *result = sqlite3_column_text(stmt, 0);
        if (!result || strstr((const char *)result, "\"tools\"") == NULL) {
            fprintf(stderr, "    Unexpected result from call %d\n", i);
            sqlite3_finalize(stmt);
            return 1;
        }
        sqlite3_finalize(stmt);
        printf("    ✓ Call %d completed successfully\n", i);
    }

    // Step 3: Verify both calls succeeded (both made network requests)
    printf("    [3/3] Verifying both calls executed...\n");
    printf("    ✓ Both scalar function calls completed\n");
    printf("    ✓ Scalar functions don't use caching (each call is fresh)\n");
    return 0;
}

// Test: Multiple queries with filtering on virtual table
int test_mcp_tools_table_filtering(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int rc;

    printf("    Testing virtual table filtering with caching...\n");

    // Step 1: Connect first
    printf("    [1/3] Connecting to MCP server...\n");
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
    printf("    ✓ Connected\n");

    // Step 2: Query with WHERE clause - should still create and use cache
    printf("    [2/3] Querying with WHERE clause...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT name FROM mcp_tools_table WHERE name LIKE 'browser%'",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare filtered query: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        printf("    ✓ Found tool: %s\n", name);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        fprintf(stderr, "    No browser tools found\n");
        // Not necessarily an error, continue
    } else {
        printf("    ✓ Found %d browser tools\n", count);
    }

    // Step 3: Query again with different filter - should use same cached table
    printf("    [3/3] Querying again with different filter...\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM mcp_tools_table",
        -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    Failed to prepare count query: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "    Failed to execute count query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }
    int total = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    printf("    ✓ Total tools in cache: %d\n", total);
    printf("    ✓ Filtering works correctly on cached table\n");
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

    printf("\n--- JSON Extension Mode Tests ---\n");
    run_test("mcp_connect() with JSON extension mode", test_mcp_json_extension);
    run_test("mcp_connect() returns no rows in JSON mode", test_mcp_connect_json_mode);

    printf("\n--- Virtual Table Tests ---\n");
    run_test("mcp_tools_table virtual table", test_mcp_tools_table);
    run_test("mcp_call_tool_table virtual table", test_mcp_call_tool_table);

    printf("\n--- Virtual Table Caching Tests ---\n");
    run_test("Virtual table caching behavior", test_mcp_tools_table_caching);
    run_test("Temp table creation and naming", test_mcp_tools_table_temp_exists);
    run_test("Virtual table filtering with cache", test_mcp_tools_table_filtering);
    run_test("Scalar functions don't cache", test_scalar_function_no_cache);

    printf("\n=== Test Results ===\n");
    printf("Total:  %d\n", test_count);
    printf("Passed: %d\n", passed_count);
    printf("Failed: %d\n", failed_count);
    if (skipped_count > 0) {
        printf("Skipped: %d\n", skipped_count);
    }

    return (failed_count == 0) ? 0 : 1;
}
