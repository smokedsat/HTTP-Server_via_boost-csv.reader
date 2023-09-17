#include "http_connection.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace target_http
{
    const char * search = "/search"; 
    const char * main = "/main";
    const char * set = "/set";
    const char * upload = "/upload";
    const char * showtable = "/showtable";
};


void 
    http_server(tcp::acceptor& acceptor, tcp::socket& socket, std::shared_ptr<Database>& sptr_database, const std::string & ip_address ,const std::string & port)
{
    acceptor.async_accept(socket,
        [&](beast::error_code ec)
        {
            if (!ec)
                std::make_shared<http_connection>(std::move(socket), sptr_database, ip_address, port)->start();
            http_server(acceptor, socket, sptr_database, ip_address, port);
        });
}

http_connection::http_connection(tcp::socket socket, std::shared_ptr<Database>& sptr_database, const std::string & port, const std::string & ip_address)
        : socket_(std::move(socket)), PORT(port), IP_ADDRESS(ip_address)
{
    this->sptr_database = sptr_database;
}

void 
    http_connection::start()
{
    read_request();
    check_deadline();
}

void 
    http_connection::read_request()
{
    auto self = shared_from_this();

    http::async_read(
        socket_,
        buffer_,
        request_,
        [self](beast::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);
            if (!ec)
                self->process_request();
        });
}

void 
    http_connection::write_response()
{
    auto self = shared_from_this();
    response_.content_length(response_.body().size());

    http::async_write(
        socket_,
        response_,
        [self](beast::error_code ec, std::size_t)
        {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            self->deadline_.cancel();
        });
}

void 
    http_connection::check_deadline()
{
    auto self = shared_from_this();

    deadline_.async_wait(
        [self](beast::error_code ec)
        {
            if (!ec)
            {
                self->socket_.close(ec);
            }
        });
}

void
    http_connection::process_request()
{
    response_.version(request_.version());
    response_.keep_alive(false);

    switch (request_.method())
    {
    case http::verb::get:
        if (request_.target().find(target_http::main) == 0)
        {
            std::cout << "Entered to get::main" << std::endl;
            response_.result(http::status::ok);
            response_.set(http::field::server, "Get request");
            response_.prepare_payload();
            create_response();
        }
        else if (request_.target().find(target_http::set) == 0)
        {
            std::cout << "Entered to get::set" << std::endl;
            if (sptr_database->tables.empty())
            {
                // Создаем HTTP-ответ
                response_.result(http::status::ok);
                response_.set(http::field::server, "Get request");
                response_.prepare_payload();

                beast::ostream(response_.body()) << "Tables is empty. Cant set table to any number with /set.\n ";
                create_response();
            }
            else
            {
                if (request_.target().size() > 5)
                {
                    // Получаем полный путь из URI запроса
                    std::string target = request_.target().substr(5).to_string();

                    int number = std::stoi(target);

                    if (sptr_database->countOfTables >= number)
                    {
                        sptr_database->currentTableId = number;
                        beast::ostream(response_.body()) << "Current table was changed to " << std::to_string(number) << "\n";
                    }
                    else
                    {
                        beast::ostream(response_.body())
                            << "Count of tables is lower than you trying to change to.\n "
                            << "<p><strong> <a href=\"/main\">Return to Main Page</a></strong></p>\n"
                            << "<form action=\"http://"
                            << IP_ADDRESS 
                            << ":"
                            << PORT
                            << "\">";
                    }
                }
            }
            // Создаем HTTP-ответ
            response_.result(http::status::ok);
            response_.set(http::field::server, "Get request");
            response_.prepare_payload();
            create_response();
        }
        else if (request_.target().find(target_http::search) == 0)
        {
            std::cout << "Entered to get::search" << std::endl;
            if (sptr_database->tables.empty())
            {
                // Создаем HTTP-ответ
                response_.result(http::status::ok);
                response_.set(http::field::server, "Get request");
                response_.prepare_payload();

                beast::ostream(response_.body()) << "Tables is empty. Cant start /search.\n ";
                create_response();
            }
            else
            {
                if (request_.target().size() > 8)
                {
                    // Получаем полный путь из URI запроса
                    std::string target_path = request_.target().substr(8).to_string();

                    // Ищем параметр column_name в URL-адресе
                    size_t param_pos = target_path.find("column_name=");

                    if (param_pos != std::string::npos)
                    {
                        // Найден параметр column_name, извлекаем его значение
                        size_t value_start = param_pos + strlen("column_name=");
                        size_t value_end = target_path.find("=", value_start);
                        std::string column_name_value = target_path.substr(value_start, value_end - value_start);

                        // Выполняем поиск по значению column_name_value
                        sptr_database->results = sptr_database->tables[sptr_database->currentTableId].getColumnsWithName(column_name_value);
                    }
                }
            }
            // Создаем HTTP-ответ
            response_.result(http::status::ok);
            response_.set(http::field::server, "Get request");
            response_.prepare_payload();
            create_response();
        }
        else if (request_.target().find(target_http::showtable) == 0)
        {
            std::cout << "Entered to get::showtable" << std::endl;
            response_.result(http::status::ok);
            response_.set(http::field::server, "Get request");

            response_.prepare_payload();
            create_response();
        }
        if (request_.target().find(target_http::upload) == 0)
        {
            std::cout << "Entered to get::upload" << std::endl;
            std::string file_data = beast::buffers_to_string(request_.body().data());

            std::cout << "body of upload: " << file_data << std::endl;

            const std::string filename = "uploaded_file_" + std::to_string(sptr_database->countOfTables) + ".csv";

            std::ofstream outfile(filename);
            outfile << file_data;
            outfile.close();

            lines_eraser(filename);
            std::string newfilename = "clear_" + filename;
            sptr_database->curr_table.readCSV(newfilename);
            sptr_database->curr_table.setTableFilename(filename);
            sptr_database->tables.emplace_back(sptr_database->curr_table);
            // start
            std::cout << newfilename << std::endl;
            sptr_database->curr_table.printTable();
            // end
            sptr_database->curr_table.clearTable();

            sptr_database->countOfTables = sptr_database->tables.size();

            response_.result(http::status::ok);
            response_.set(http::field::server, "Post request");
            beast::ostream(response_.body()) << "File was successfully uploaded.\n" << "Count of loaded files: " << sptr_database->tables.size() << "\n";
            response_.prepare_payload();
            create_response();
        }
        else
        {
            response_.result(http::status::ok);
            response_.set(http::field::server, "Get request");
            response_.prepare_payload();
            create_response();
        }
        break;
    case http::verb::post:
        if (request_.target().find(target_http::upload) == 0)
        {
            std::cout << "Entered to post::upload" << std::endl;
            std::string file_data = beast::buffers_to_string(request_.body().data());

            std::cout << "body of upload: " << file_data << std::endl;

            const std::string filename = "uploaded_file_" + std::to_string(sptr_database->countOfTables) + ".csv";

            std::ofstream outfile(filename);
            outfile << file_data;
            outfile.close();

            lines_eraser(filename);
            std::string newfilename = "clear_" + filename;
            sptr_database->curr_table.readCSV(newfilename);
            sptr_database->curr_table.setTableFilename(filename);
            sptr_database->tables.emplace_back(sptr_database->curr_table);
            // start
            std::cout << newfilename << std::endl;
            sptr_database->curr_table.printTable();
            // end
            sptr_database->curr_table.clearTable();

            sptr_database->countOfTables = sptr_database->tables.size();

            response_.result(http::status::ok);
            response_.set(http::field::server, "Post request");
            beast::ostream(response_.body()) << "File was successfully uploaded.\n" << "Count of loaded files: " << sptr_database->tables.size() << "\n";
            response_.prepare_payload();
            create_response();
        }
        else
        {
            std::cout << "Entered to post::bad_request" << std::endl;
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "No file data found in the request\r\n";
        }
        break;
    default:
        response_.result(http::status::bad_request);
        response_.set(http::field::content_type, "text/plain");
        beast::ostream(response_.body())
            << "Invalid request-method '"
            << std::string(request_.method_string())
            << "'";
        break;
    }
    write_response();
}

void 
    http_connection::create_response()
{
    if (request_.target().find(target_http::main) == 0)
    {
        response_.result(http::status::ok);
        response_.set(http::field::server, "Get request");
        response_.set(http::field::content_type, "text/html");
        beast::ostream(response_.body())
            << "<html>\n"
            << "<head><title>Main Page</title></head>\n"
            << "<body>\n"
            << "<h1>Main Page</h1>\n"
            << "<p>Welcome to the main page. Choose an action:</p>\n"
            << "<ul>\n"
            << "<li><a href=\"/upload\">Upload a file</a></li>\n";
            
        if (!sptr_database->tables.empty())
        {
            for (size_t count = 0; count < sptr_database->countOfTables; count++)
            {
                beast::ostream(response_.body()) << "<li>Set table to <a href=\"/set/" + std::to_string(count);
                beast::ostream(response_.body()) << "\"> ";
                beast::ostream(response_.body()) << sptr_database->tables[count].table_filename;
                beast::ostream(response_.body()) << " </a></li>\n";
            }
            beast::ostream(response_.body()) << "<br>\n";
        
            // Button showtable
            beast::ostream(response_.body())
                << "<form action=\"http://" << IP_ADDRESS << ":" << PORT
                << "/showtable/\" method=\"get\">\n" // Используем метод GET
                << "<li><b>Enter Table Number:   </b> <input type=\"number\" name=\"tableNumber\">\n"
                << "    <input type=\"submit\" value=\"Show Table\"></li>\n"
                << "</form>\n";

            // Button search 
            beast::ostream(response_.body())
                << "<b><p>Search in table</p></b>"
                << "<form action=\"/search\" method=\"get\">\n"
                << "    <label for=\"column_name\">Column Name:</label>\n"
                << "    <input type=\"text\" id=\"column_name\" name=\"column_name\">"
                << "    <input type=\"submit\" value=\"Search\">\n"
                << "</form>\n";

            // Getting column_names from table
            sptr_database->results = sptr_database->tables[sptr_database->currentTableId].getColumnNames();

            std::vector<std::string> currentTableNames = sptr_database->results;

            beast::ostream(response_.body())
                << "<br><strong>Column Names:</strong><ul>"
                << "<strong>";

            for (const std::string& result : currentTableNames)
            {
                beast::ostream(response_.body()) << "<tr>" << result << "</tr>";
            }
            
            // Getting column_types from table
            sptr_database->results = sptr_database->tables[sptr_database->currentTableId].getColumnTypes();
            std::vector<std::string> currentTableTypes = sptr_database->results;

            beast::ostream(response_.body())
                << "</strong><strong>Column Types:</strong>\n" << "<strong>";

            for (const std::string& result : currentTableTypes)
            {
                beast::ostream(response_.body()) << "<tr>" << result << "</tr>";
            }
            beast::ostream(response_.body()) << "</strong>" << "</ul>";
        }

        beast::ostream(response_.body())
            << "</body>\n"
            << "</html>\n";
    }
    else if (request_.target().find(target_http::upload) == 0)
    {
        response_.set(http::field::content_type, "text/html");
        beast::ostream(response_.body())
            << "<html>\n"
            << "<head>\n"
            << "<title> File Upload </title> \n"
            << "</head>\n"
            << "<body>\n"
            << "<h1> File upload </h1>\n"
            << "<p> <a href=\"/main\">Return to Main Page</a></p>\n"
            << "<form action=\"http://" << IP_ADDRESS << ":" << PORT
            << "/upload\" method=\"post\" enctype=\"multipart/form-data\">\n"
            << "<input type=\"file\" name=\"uploaded-file\">\n"
            << "<input type=\"submit\" value=\"Upload\">\n"
            << "</form>\n"
            << "</body>\n"
            << "</html>\n";
    }
    else if (request_.target().find(target_http::search) == 0)
    {
        response_.set(http::field::content_type, "text/html");
        beast::ostream(response_.body()) << generateDynamicResponse(sptr_database->results, IP_ADDRESS, PORT);
    }
    else if (request_.target().find(target_http::showtable) == 0)
    {
        if (!sptr_database->tables.empty())
        {
            size_t target_size = request_.target().size();
            if (target_size <= 11)
            {
                response_.set(http::field::content_type, "text/html");
                beast::ostream(response_.body()) << "Please make sure you wrote number of table you want to show. Example /showtable/0 \n";
                return;
            }
            else if (target_size > 24)
            {
                target_size = 24;
            }
            else if (target_size > 11)
            {
                target_size = 11;
            }

            std::string target = request_.target().substr(target_size).to_string();
            size_t table_id = std::stoi(target);

            if (table_id <= sptr_database->countOfTables)
            {
                if (sptr_database->countOfTables == 1)
                {
                    table_id = 0;
                    sptr_database->currentTableId = table_id;
                }
                if (table_id >= sptr_database->tables.size())
                {
                    beast::ostream(response_.body()) << "Table with that big number was not found\n";
                }
                else
                {
                    beast::ostream(response_.body()) << generateDynamicResponse(sptr_database->tables[table_id], IP_ADDRESS, PORT);
                }
            }
            else
            {
                sptr_database->currentTableId = 0;
                // Изменить на generateDynamicEmptyResponse(std::string & resp)
                beast::ostream(response_.body()) << "Table with that number was not found.\n";
            }
        }
        else
        {
            response_.set(http::field::content_type, "text/html");
            // Изменить на generateDynamicEmptyTableResponse(std::string & resp
            beast::ostream(response_.body()) << "tables is empty. Cant show any data with /showtable. \n";
        }
    }
    else if (request_.target().find(target_http::set) == 0)
    {
        response_.set(http::field::content_type, "text/html");
        beast::ostream(response_.body()) << "<p><strong> <a href=\"/main\">Return to Main Page</a></strong></p>\n";
        beast::ostream(response_.body()) << "<form action=\"http://"<< IP_ADDRESS << ":" << PORT;
        beast::ostream(response_.body()) << "\">";
    }
}


