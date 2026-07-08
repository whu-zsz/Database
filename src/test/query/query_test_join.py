import os
import time

NUM_TESTS = 2

def get_test_name(index):
    return "src/test/query/query_sql/join_test" + str(index) + ".sql"

def get_output_name(index):
    return "src/test/query/query_sql/join_answer" + str(index) + ".txt"

def build():
    if os.path.exists("./build"):
        os.system("rm -rf build")
    os.mkdir("./build")
    os.chdir("./build")
    ret = os.system("cmake ..")
    if ret != 0:
        print("cmake failed")
        exit(1)
    ret = os.system("make rmdb -j4")
    if ret != 0:
        print("make rmdb failed")
        exit(1)
    ret = os.system("make query_test -j4")
    if ret != 0:
        print("make query_test failed")
        exit(1)
    os.chdir("..")


def run():
    os.chdir("./build")
    score = 0.0

    for i in range(NUM_TESTS):
        test_file = "../" + get_test_name(i + 1)
        database_name = "join_test_db"

        if os.path.exists(database_name):
            os.system("rm -rf " + database_name)
        os.system("pkill -9 rmdb 2>/dev/null")
        time.sleep(1)

        print("Starting server for join test", i + 1, "...")
        os.system("./bin/rmdb " + database_name + " &")
        time.sleep(3)

        ret = os.system("./bin/query_test " + test_file)
        if ret != 0:
            print("query_test returned error. Stopping")
            os.system("pkill -9 rmdb 2>/dev/null")
            exit(1)

        # Check answer
        ansDict = {}
        standard_answer = "../" + get_output_name(i + 1)
        try:
            hand0 = open(standard_answer, "r")
            for line in hand0:
                line = line.strip('\n')
                if line == "":
                    continue
                num = ansDict.setdefault(line, 0)
                ansDict[line] = num + 1
            hand0.close()
        except FileNotFoundError:
            print("Answer file not found:", standard_answer)
            os.system("pkill -9 rmdb 2>/dev/null")
            exit(1)

        my_answer = database_name + "/output.txt"
        try:
            hand1 = open(my_answer, "r")
            for line in hand1:
                line = line.strip('\n')
                if line == "":
                    continue
                num = ansDict.setdefault(line, 0)
                ansDict[line] = num - 1
            hand1.close()
        except FileNotFoundError:
            print("Output file not found:", my_answer)

        match = True
        for key, value in ansDict.items():
            if value != 0:
                match = False
                if value > 0:
                    print('Mismatch, your answer lacks: ' + key)
                else:
                    print('Mismatch, your answer has extra: ' + key)

        if match:
            score += 50.0
            print("Test", i + 1, "PASSED")
        else:
            print("Test", i + 1, "FAILED")

        os.system("pkill -9 rmdb 2>/dev/null")
        time.sleep(1)
        if os.path.exists(database_name):
            os.system("rm -rf ./" + database_name)

    os.chdir("..")
    print("Final score: " + str(score) + " / 100")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, "../../.."))
    os.chdir(project_root)
    print("Project root:", os.getcwd())
    build()
    run()
