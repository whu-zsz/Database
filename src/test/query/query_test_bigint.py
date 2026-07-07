import os
import time

# test : bigint
NUM_TESTS = 1
SCORES = [100]

# current dir is PROJECT_ROOT (rmdb/)
def get_test_name(index):
    return "src/test/query/query_sql/storage_test" + str(index) + ".sql"

def get_output_name(index):
    return "src/test/query/query_sql/storage_test" + str(index) + "_answer.txt"

def build():
    # 确保在项目根目录
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
    # 确保在项目根目录
    os.chdir("./build")
    score = 0.0

    for i in range(NUM_TESTS):
        test_file = "../" + get_test_name(i + 6)  # storage_test6.sql
        database_name = "bigint_test_db"

        # 清理上一轮残留
        if os.path.exists(database_name):
            os.system("rm -rf " + database_name)
        # 确保没有残留进程
        os.system("pkill -9 rmdb 2>/dev/null")
        time.sleep(1)

        # 启动服务端
        print("Starting server for test", i + 6, "...")
        os.system("./bin/rmdb " + database_name + " &")
        time.sleep(3)

        # 运行测试
        ret = os.system("./bin/query_test " + test_file)
        if ret != 0:
            print("query_test returned error. Stopping")
            os.system("pkill -9 rmdb 2>/dev/null")
            exit(1)

        # 检查结果
        ansDict = {}
        standard_answer = "../" + get_output_name(i + 6)
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
                    print('In bigint test' + str(i + 6) + ', Mismatch, your answer lacks: ' + key)
                else:
                    print('In bigint test' + str(i + 6) + ', Mismatch, your answer has extra: ' + key)

        if match:
            score += SCORES[i]
            print("Test", i + 6, "PASSED (score:", SCORES[i], ")")
        else:
            print("Test", i + 6, "FAILED")

        # 关闭服务端
        os.system("pkill -9 rmdb 2>/dev/null")
        time.sleep(1)
        print("Server killed")

        # 清理数据库
        if os.path.exists(database_name):
            os.system("rm -rf ./" + database_name)

    os.chdir("..")
    print("Final score: " + str(score) + " / 100")


if __name__ == "__main__":
    # 确保从项目根目录开始
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # 脚本在 src/test/query/ 下，项目根目录在 ../../../
    project_root = os.path.abspath(os.path.join(script_dir, "../../.."))
    os.chdir(project_root)
    print("Project root:", os.getcwd())

    build()
    run()
