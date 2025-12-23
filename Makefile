CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS  = 

INCLUDES = -Iinclude
SRC_DIR  = src

ES_OBJS   = $(SRC_DIR)/es_main.o $(SRC_DIR)/es_server.o $(SRC_DIR)/common.o $(SRC_DIR)/protocol.o
USER_OBJS = $(SRC_DIR)/user_main.o $(SRC_DIR)/user_client.o $(SRC_DIR)/common.o $(SRC_DIR)/protocol.o

TARGET_ES   = ES
TARGET_USER = user

all: $(TARGET_ES) $(TARGET_USER)

$(TARGET_ES): $(ES_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_USER): $(USER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET_ES) $(TARGET_USER)

.PHONY: all clean
