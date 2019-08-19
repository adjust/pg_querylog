#coding: utf-8

import multiprocessing
import testgres


def running_queries(node):
    res = node.safe_psql("select * from running_queries")
    assert(len(res) == 3)


def all_queries(node):
    res = node.safe_psql("select * from get_queries()")
    assert(len(res) == 3)


def make_queries(node, alive):
    with multiprocessing.Pool(processes=10) as pool:
        while alive.value:
            pool.apply_async(running_queries, (node, ))
            pool.apply_async(all_queries, (node, ))


def test_session_preload():
    with testgres.get_new_node("master") as node:
        node.init()
        node.append_conf(session_preload_libraries='pg_querylog')
        node.start()
        node.psql("create extension pg_querylog")
        node.pgbench_run(initialize=True, scale=2)

        alive = multiprocessing.Value('i', 1)
        reader = multiprocessing.Process(target=make_queries, args=(node, alive))
        reader.start()

        node.pgbench_run(time=30, client=multiprocessing.cpu_count())
        node.pgbench_run(time=30, client=multiprocessing.cpu_count(), connect=True)
        alive.value = 0
        reader.join()
