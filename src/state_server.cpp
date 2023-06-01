// Copyright (c) 2023, Hoang Giang Nguyen - Institute for Artificial Intelligence, University Bremen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <ros/ros.h>

#include <string>
#include <chrono>
#include <iostream>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <thread>
#include <mutex>

#include <zmq.hpp>

#include "state_msgs.cpp"

using namespace std::chrono_literals;

int port_header = 7500;
int port_data = 7600;

zmq::context_t context{1};

std::mutex mtx;

std::map<std::string, std::map<std::string, std::vector<double>>> publisher_data;

void worker(const size_t thread_num, const Json::Value &json_header)
{
    Json::Value publishers = json_header["publishers"];

    std::vector<double*> published_data_vec;
    mtx.lock();
    for (auto it = publishers.begin(); it != publishers.end(); ++it)
    {
        const std::string object_name = it.key().asString();
        publisher_data[object_name] = {};
        for (const auto& attr : *it)
        {
            const std::string attribute_name = attr.asString();
            publisher_data[object_name][attribute_name] = attribute_map[attribute_name];
            for (double &value : publisher_data[object_name][attribute_name])
            {
                published_data_vec.push_back(&value);
            }
        }
    }
    mtx.unlock();

    Json::Value subscribers = json_header["subscribers"];
    std::vector<double*> subscribed_data_vec;
    
    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        const std::string object_name = it.key().asString();
        while (publisher_data.count(object_name) == 0)
        {
            zmq_sleep(0.1);
        }
        
        for (const auto& attr : *it)
        {
            const std::string attribute_name = attr.asString();
            for (double &value : publisher_data[object_name][attribute_name])
            {
                subscribed_data_vec.push_back(&value);
            }
        }
    }

    zmq::socket_t socket_data{context, zmq::socket_type::rep};
    const std::string addr = "tcp://127.0.0.1:" + std::to_string(port_data + thread_num);
    socket_data.bind(addr);

    const size_t published_data_size = 1 + published_data_vec.size();
    double published_buffer[published_data_size];

    const size_t subscribed_data_size = 1 + subscribed_data_vec.size();
    double subscribed_buffer[subscribed_data_size];

    while (ros::ok()) 
    {
        zmq::message_t request(sizeof(published_buffer));
        socket_data.recv(request);
        memcpy(&published_buffer, request.data(), sizeof(published_buffer));

        for (size_t i = 0; i < published_data_size - 1; i++)
        {
            *published_data_vec[i] = published_buffer[i+1];
        }

        subscribed_buffer[0] = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        for (size_t i = 1; i < subscribed_data_size; i++)
        {
            subscribed_buffer[i] = *subscribed_data_vec[i-1];
        }
        
        zmq::message_t reply(sizeof(subscribed_buffer));
        memcpy(reply.data(), &subscribed_buffer, sizeof(subscribed_buffer));
        socket_data.send(reply, zmq::send_flags::none);
    }

    socket_data.unbind(addr);
}

int main(int argc, char **argv) 
{
    ros::init(argc, argv, "state_server");

    if (argc > 2)
    {
        port_header = std::stoi(argv[1]);
        port_data = std::stoi(argv[2]);
    }

    std::vector<zmq::socket_t> socket_headers;
    std::vector<std::thread> workers;

    for (size_t i = 0;; i++)
    {
        socket_headers.push_back(zmq::socket_t(context, zmq::socket_type::rep));
        socket_headers[i].bind("tcp://127.0.0.1:" + std::to_string(port_header + i));
        
        zmq::message_t reply_header;
        socket_headers[i].recv(reply_header);
    
        Json::Value json_header;
        Json::Reader reader;
        reader.parse(reply_header.to_string(), json_header);
        std::cout << json_header.toStyledString() << std::endl;
        
        workers.push_back(std::thread(worker, i, json_header));
    }

    for (std::thread& worker : workers) 
    {
        worker.join();
    }

    return 0;
}
