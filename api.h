#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>
#include <boost/spirit/include/qi.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

//------------------------------------------------------------------------------

// Функция для чтения CSV-файла и возврата его содержимого в виде вектора строк
std::vector<std::vector<std::string>> read_csv(const std::string& filename) {
    std::ifstream file(filename);
    std::vector<std::vector<std::string>> data;
    std::string line;
    while (std::getline(file, line)) {
        std::vector<std::string> row;
        boost::spirit::qi::parse(
            line.begin(), line.end(),
            // Грамматика для разбора CSV-файла
            (boost::spirit::qi::raw[+(boost::spirit::qi::char_ - ',')] % ','),
            row);
        data.push_back(row);
    }
    return data;
}

// Функция для обработки HTTP-запросов
template<class Body, class Allocator, class Send>
void handle_request(
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{
    // Ответ на запрос
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());

    // Обработка запросов к различным путям
    if (req.target() == "/files") {
        // Получение списка файлов с информацией о колонках
        // ...
        res.body() = "Список файлов";
    } else if (req.target() == "/data") {
        // Получение данных из конкретного файла с опциональными фильтрацией и сортировкой по одному или нескольким столбцам
        // ...
        res.body() = "Данные из файла";
    } else {
        res.result(http::status::not_found);
        res.body() = "Not Found";
    }

    res.prepare_payload();
    return send(std::move(res));
}

//------------------------------------------------------------------------------

// Этот класс представляет собой сессию, которая выполняется для каждого соединения
class session : public std::enable_shared_from_this<session>
{
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;

public:
    explicit session(tcp::socket&& socket)
        : stream_(std::move(socket))
    {
    }

    void run()
    {
        do_read();
    }

private:
    void do_read()
    {
        auto self = shared_from_this();
        http::async_read(stream_, buffer_, req_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);
                if (!ec)
                    self->do_write();
            });
    }

    void do_write()
    {
        auto self = shared_from_this();
        handle_request(std::move(req_),
            [self](auto&& response) {
                auto sp = std::make_shared<
                    http::response<http::string_body>>(
                        std::forward<decltype(response)>(response));
                http::async_write(self->stream_, *sp,
                    [self, sp](beast::error_code ec, std::size_t bytes_transferred) {
                        boost::ignore_unused(bytes_transferred);
                        self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
                    });
            });
    }
};

//------------------------------------------------------------------------------

// Этот класс принимает входящие соединения и создает сессии
class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    listener(
        net::io_context& ioc,
        tcp::endpoint endpoint)
        : ioc_(ioc)
        , acceptor_(ioc)
    {
        beast::error_code ec;

        // Открытие акцептора
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            fail(ec, "open");
            return;
        }

        // Разрешение повторного использования адреса
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            fail(ec, "set_option");
            return;
        }

        // Привязка акцептора к адресу
        acceptor_.bind(endpoint, ec);
        if (ec) {
            fail(ec, "bind");
            return;
        }

        // Начало прослушивания входящих соединений
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            fail(ec, "listen");
            return;
        }
    }

    void run()
    {
        do_accept();
    }

private:
    void do_accept()
        {
            auto self = shared_from_this();
            acceptor_.async_accept(
                [self](beast::error_code ec, tcp::socket socket) {
                    if (!ec)
                        std::std::make_shared<session>(std::move(socket))->run();
                        self->do_accept();
                }
            ); 
        } 
    void fail(beast::error_code ec, char const* what) 
    { 
        std::cerr << what << ": " << ec.message() << "\n"; 
    } 
};

