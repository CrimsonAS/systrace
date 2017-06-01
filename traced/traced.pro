CONFIG -= app_bundle
TEMPLATE = app
TARGET = traced
INCLUDEPATH += .

linux:LIBS += -lrt

# Input
SOURCES += main.cpp
