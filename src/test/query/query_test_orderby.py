import os
import time

# test : order_by
# SQL test file contains multiple SELECT queries with ORDER BY and LIMIT

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

    test_file = "../src/test/query/query_sql/order_by_test.sql"
    standard_answer = "../src/test/query/query_sql/order_by_answer.txt"
    database_name = "order_by_test_db"

    # 清理上一轮残留
    if os.path.exists(database_name):
        os.system("rm -rf " + database_name)
    os.system("pkill -9 rmdb 2>/dev/null")
    time.sleep(1)

    # 启动服务端
    print("Starting server for order_by test...")
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
                print('Mismatch, your answer lacks: ' + key)
            else:
                print('Mismatch, your answer has extra: ' + key)

    if match:
        score = 100.0
        print("order_by test PASSED (score: 100)")
    else:
        print("order_by test FAILED")

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
    project_root = os.path.abspath(os.path.join(script_dir, "../../.."))
    os.chdir(project_root)
    print("Project root:", os.getcwd())

    build()
    run()
