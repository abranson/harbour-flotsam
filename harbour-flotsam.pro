TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    src/app \
    src/syncd \
    src/helper

tests.file = tests/tests.pro
tests.depends = src/helper

contains(CONFIG, flotsam_tests) {
    SUBDIRS += tests
}
