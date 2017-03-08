QT =
CONFIG -= app_bundle
TEMPLATE = app
TARGET = systrace
INCLUDEPATH += . ..
INCLUDEPATH += ../traced

# Input
HEADERS += CSystrace.h
SOURCES += ../unix/CSystrace.cpp \
           main.cpp
