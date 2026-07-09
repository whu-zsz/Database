import os
import shutil
import subprocess
import time

TESTS = [
    ("transaction_test1", "transaction_test1_db"),
    ("transaction_test2", "transaction_test2_db"),
    ("transaction_test3", "transaction_test3_db"),
    ("transaction_test4", "transaction_test4_db"),
]


def project_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))


def build():
    if os.path.exists("build"):
        shutil.rmtree("build")
    os.mkdir("build")
    subprocess.check_call(["cmake", ".."], cwd="build")
    subprocess.check_call(["make", "rmdb", "query_test", "-j4"], cwd="build")


def compare_answer(db_name, answer_file):
    expected = {}
    with open(answer_file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line:
                expected[line] = expected.get(line, 0) + 1
    output = os.path.join("build", db_name, "output.txt")
    with open(output, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line:
                expected[line] = expected.get(line, 0) - 1
    return {k: v for k, v in expected.items() if v != 0}


def stop_server():
    subprocess.call("pkill -9 rmdb 2>/dev/null", shell=True)
    time.sleep(1)


def run():
    score = 0
    for test_name, db_name in TESTS:
        db_path = os.path.join("build", db_name)
        if os.path.exists(db_path):
            shutil.rmtree(db_path)
        stop_server()
        server = subprocess.Popen(["./bin/rmdb", db_name], cwd="build")
        time.sleep(3)
        try:
            sql_file = os.path.join("..", "src", "test", "query", "query_sql", test_name + ".sql")
            subprocess.check_call(["./bin/query_test", sql_file], cwd="build")
        finally:
            stop_server()
            server.poll()
        answer_file = os.path.join("src", "test", "query", "query_sql", test_name + "_answer.txt")
        diff = compare_answer(db_name, answer_file)
        if diff:
            print(test_name, "FAILED")
            for line, count in diff.items():
                direction = "lacks" if count > 0 else "extra"
                print(" ", direction, line)
        else:
            print(test_name, "PASSED")
            score += 1
        if os.path.exists(db_path):
            shutil.rmtree(db_path)
    print("Final score:", score, "/", len(TESTS))
    if score != len(TESTS):
        raise SystemExit(1)


if __name__ == "__main__":
    os.chdir(project_root())
    build()
    run()
