#include "mg/connection.h"

using namespace mg;

connection::connection(mg_connection* c) noexcept : _connection(c) {
}