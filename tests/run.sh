#!/bin/sh
# Automated test suite for todoc
# Usage: ./tests/run.sh [--valgrind]
#
# Runs all commands against a temporary database and checks exit codes
# and expected output. With --valgrind, also checks for memory leaks.

set -e

TODOC="./build/todoc"
TEST_HOME=$(mktemp -d)
export HOME="$TEST_HOME"

PASS=0
FAIL=0
USE_VALGRIND=0

if [ "$1" = "--valgrind" ]; then
    USE_VALGRIND=1
    if ! command -v valgrind > /dev/null 2>&1; then
        echo "ERROR: valgrind not found"
        exit 1
    fi
fi

# ── Helpers ──────────────────────────────────────────────────────

cleanup() {
    rm -rf "$TEST_HOME"
}
trap cleanup EXIT

run() {
    if [ "$USE_VALGRIND" = 1 ]; then
        valgrind --leak-check=full --show-leak-kinds=all \
                 --error-exitcode=99 --quiet \
                 "$TODOC" "$@" 2>&1
    else
        "$TODOC" "$@" 2>&1
    fi
}

# assert_ok: run command, expect exit 0
assert_ok() {
    desc="$1"
    shift
    if output=$(run "$@"); then
        PASS=$((PASS + 1))
        printf "  \033[32mPASS\033[0m  %s\n" "$desc"
    else
        rc=$?
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (exit %d)\n" "$desc" "$rc"
        printf "        %s\n" "$output"
    fi
}

# assert_fail: run command, expect non-zero exit
assert_fail() {
    desc="$1"
    shift
    if output=$(run "$@"); then
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (expected failure, got exit 0)\n" "$desc"
        printf "        %s\n" "$output"
    else
        rc=$?
        # exit 99 = valgrind found a leak, that's a real failure
        if [ "$rc" = 99 ]; then
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (memory error)\n" "$desc"
            printf "        %s\n" "$output"
        else
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        fi
    fi
}

# assert_output: run command, expect exit 0 and output containing a string
assert_output() {
    desc="$1"
    expected="$2"
    shift 2
    if output=$(run "$@"); then
        if echo "$output" | grep -qF "$expected"; then
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        else
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (expected output containing '%s')\n" "$desc" "$expected"
            printf "        got: %s\n" "$output"
        fi
    else
        rc=$?
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (exit %d)\n" "$desc" "$rc"
        printf "        %s\n" "$output"
    fi
}

# ── Check binary exists ─��───────────────────────────────────────

if [ ! -x "$TODOC" ]; then
    echo "ERROR: $TODOC not found. Run 'make' first."
    exit 1
fi

echo ""
echo "todoc test suite"
if [ "$USE_VALGRIND" = 1 ]; then
    echo "(with valgrind leak checking)"
fi
echo "─────────────────────────────────────"
echo ""

# ── 1. Init ─────────────────────────────────────────────────────

echo "Init:"
assert_ok      "init creates database"                init
assert_output  "init is idempotent"  "up to date"     init
echo ""

# ── 2. Add ──────────────────────────────────────────────────────

echo "Add:"
assert_ok      "add basic task"                       add "Buy milk"
assert_ok      "add with all options"                 add "Fix login bug" --type bug --priority critical --scope auth --desc "SSO broken" --due 2026-05-01
assert_ok      "add feature"                          add "Dark mode" --type feature --priority medium --scope ui
assert_ok      "add chore"                            add "Update deps" --type chore --priority low
assert_ok      "add idea"                             add "AI search" --type idea --priority high --scope search
assert_fail    "add without title fails"              add
assert_fail    "add with bad type fails"              add "x" --type invalid
assert_fail    "add with bad priority fails"          add "x" --priority urgent
assert_fail    "add with bad date fails"              add "x" --due "not-a-date"
assert_fail    "add with bad date range fails"        add "x" --due "2026-02-30"
echo ""

# ���─ 3. List ──────────────────────���──────────────────────────────

echo "List:"
assert_output  "list shows all tasks"        "5 task(s)"      list
assert_output  "list filter by priority"     "1 task(s)"      list --priority critical
assert_output  "list filter by type"         "1 task(s)"      list --type chore
assert_output  "list filter by scope"        "auth"           list --scope auth
assert_output  "list with limit"             "2 task(s)"      list --limit 2
assert_output  "list alias ls works"         "5 task(s)"      ls
echo ""

# ── 4. Show ─────────────────────────────────────────────────────

echo "Show:"
assert_output  "show displays task"          "Fix login bug"  show 2
assert_output  "show displays description"   "SSO broken"     show 2
assert_output  "show displays scope"         "auth"           show 2
assert_output  "show displays due date"      "2026-05-01"     show 2
assert_fail    "show non-existent fails"                      show 999
assert_fail    "show without id fails"                        show
echo ""

# ── 5. Edit ────────────────────────���────────────────────────────

echo "Edit:"
assert_ok      "edit title"                           edit 1 --title "Buy oat milk"
assert_output  "edit title persisted"    "oat milk"   show 1
assert_ok      "edit priority"                        edit 1 --priority high
assert_ok      "edit status"                          edit 1 --status in-progress
assert_ok      "edit multiple fields"                 edit 3 --priority high --scope frontend --due 2026-06-01
assert_fail    "edit non-existent fails"              edit 999 --title "x"
assert_fail    "edit without id fails"                edit
echo ""

# ── 6. Done ─────────────────────────��───────────────────────────

echo "Done:"
assert_ok      "done marks task"                      done 4
assert_output  "done status persisted"   "done"       show 4
assert_fail    "done non-existent fails"              done 999
echo ""

# ── 7. Delete ───────────────────────────────────────────────────

echo "Delete:"
assert_ok      "rm deletes task"                      rm 4
assert_fail    "rm non-existent fails"                rm 4
assert_fail    "rm without id fails"                  rm
assert_output  "delete alias works"      "deleted"    delete 5
assert_output  "list after deletes"      "3 task(s)"  list
echo ""

# ── 8. Stats ──────────���─────────────────────────��───────────────

echo "Stats:"
assert_output  "stats shows total"       "Total tasks: 3"   stats
assert_output  "stats shows status"      "in-progress"      stats
echo ""

# ── 8b. Export ──────────────────────────────────────────────────

echo "Export:"
assert_output  "export csv header"       "id,title,description"  export
assert_output  "export csv has data"     "Buy oat milk"          export
assert_output  "export json format"      "\"id\":"               export --format json
assert_output  "export json has data"    "Buy oat milk"          export --format json
assert_output  "export csv filtered"     "in-progress"           export --status in-progress
assert_fail    "export bad format fails"                         export --format xml
echo ""

# ── 8c. Projects ────────────────────────────────────────────────

echo "Projects:"
assert_ok      "add-project basic"                    add-project auth
assert_ok      "add-project with all options"         add-project ui --desc "Frontend redesign" --color blue --due 2026-09-01
assert_ok      "add-project minimal"                  add-project backend
assert_fail    "add-project without name fails"       add-project
assert_fail    "add-project duplicate name fails"     add-project auth

assert_output  "list-projects shows all"     "3 project(s)"        list-projects
assert_output  "list-projects shows name"    "auth"                list-projects
assert_output  "list-projects filter status" "3 project(s)"        list-projects --status active

assert_output  "show-project displays name"        "auth"             show-project auth
assert_output  "show-project displays description" "Frontend"         show-project ui
assert_output  "show-project displays color"       "blue"             show-project ui
assert_output  "show-project displays due date"    "2026-09-01"       show-project ui
assert_fail    "show-project non-existent fails"                      show-project nope
assert_fail    "show-project without name fails"                      show-project

assert_ok      "edit-project description"             edit-project auth --desc "Authentication system"
assert_output  "edit-project desc persisted" "Authentication" show-project auth
assert_ok      "edit-project status"                  edit-project backend --status completed
assert_output  "edit-project status persisted" "completed"    show-project backend
assert_ok      "edit-project color"                   edit-project auth --color red
assert_fail    "edit-project non-existent fails"      edit-project nope --desc "x"
echo ""

# ── 8d. Task assignment ─────────────────────────────────────────

echo "Assignment:"
assert_ok      "add task with --project"              add "Login form" --type feature --project auth
assert_output  "task auto-assigned to project" "Login form" list --project auth
assert_ok      "assign existing task to project"      assign 1 auth
assert_ok      "assign task to second project"        assign 1 ui
assert_output  "task visible in first project"  "Buy oat milk" list --project auth
assert_output  "task visible in second project" "Buy oat milk" list --project ui
assert_ok      "unassign task from project"           unassign 1 ui
assert_fail    "unassign already unlinked fails"      unassign 1 ui
assert_fail    "assign non-existent task fails"       assign 999 auth
assert_fail    "assign to non-existent project fails" assign 1 nope
assert_fail    "assign without args fails"            assign
echo ""

# ── 8e. Active project ──────────────────────────────────────────

echo "Active project:"
assert_ok      "use sets active project"              use auth
assert_output  "list scoped to active project" "task(s)"     list
assert_output  "list shows active project info" "auth"       list
assert_output  "list --all overrides active"   "task(s)"     list --all
assert_output  "stats scoped to active project" "Total tasks" stats
assert_ok      "use --clear removes active"           use --clear
assert_output  "list after clear shows all"   "task(s)"      list
assert_fail    "use non-existent project fails"       use nope
echo ""

# ── 8f. Project deletion ────────────────────────────────────────

echo "Project delete:"
assert_ok      "rm-project deletes project"           rm-project backend
assert_output  "list-projects after delete"  "2 project(s)" list-projects
assert_fail    "rm-project non-existent fails"        rm-project backend
assert_ok      "task survives project deletion"       show 1
assert_ok      "rm-project clears active if matched"  use auth
assert_ok      "rm active project"                    rm-project auth
echo ""

# ── 9. Help / Version ───────���───────────────────────────────────

echo "Help & Version:"
assert_output  "help shows usage"        "Usage:"             help
assert_output  "--help works"            "Usage:"             --help
assert_output  "-h works"                "Usage:"             -h
assert_output  "help task topic"         "Task commands"      help task
assert_output  "help project topic"      "Project commands"   help project
assert_output  "help export topic"       "Export"             help export
assert_output  "help command add"        "Add a new task"     help add
assert_output  "help command use"        "active project"     help use
assert_output  "help command update"     "latest release"     help update
assert_output  "help alias ls"           "List tasks"         help ls
assert_output  "update listed in help"   "update"             help
assert_fail    "help unknown topic"                           help nonsense
VERSION=$(cat .version 2>/dev/null | tr -d '[:space:]')
assert_output  "version shows number"    "$VERSION"           version
assert_output  "--version works"         "$VERSION"           --version
echo ""

# ── 10. Error handling ──────────────────────────────────────────

echo "Error handling:"
assert_fail    "unknown command"                      foobar
assert_fail    "unknown flag"                         list --unknown
assert_fail    "invalid task id"                      show abc
assert_fail    "negative task id"                     show -1
echo ""

# ── Summary ──────────────────────────���──────────────────────────

echo "─────────────────────────────────────"
TOTAL=$((PASS + FAIL))
if [ "$FAIL" = 0 ]; then
    printf "\033[32mAll %d tests passed.\033[0m\n" "$TOTAL"
else
    printf "\033[31m%d/%d tests failed.\033[0m\n" "$FAIL" "$TOTAL"
fi
echo ""

exit "$FAIL"
