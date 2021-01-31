include_directories(..)

add_executable(test_empty test_empty.c)
add_test(test_empty test_empty)

add_executable(test_actor test.c)

add_executable(test_sigint test_sigint.c)

add_executable(deadLock_test deadLock_test.c)
add_test(deadLock_test deadLock_test)

add_executable(actorSystem_test actorSystem_test.c)
add_test(actorSystem_test actorSystem_test)

set_tests_properties(test_empty PROPERTIES TIMEOUT 1)
