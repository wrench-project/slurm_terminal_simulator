#include "workflow_manager.h"

#include <random>
#include <iostream>

namespace wrench {

    WorkflowManager::WorkflowManager(
        const std::set<std::shared_ptr<ComputeService>> &compute_services,
        const std::set<std::shared_ptr<StorageService>> &storage_services,
        const std::string &hostname,
        const int node_count) : node_count(node_count), WMS(
            nullptr, nullptr,
            compute_services,
            storage_services,
            {}, nullptr,
            hostname,
            "WorkflowManager"
        ) { }
    
    int WorkflowManager::main()
    {
        this->job_manager = this->createJobManager();

        auto batch_service = *(this->getAvailableComputeServices<BatchComputeService>().begin());

        while(true)
        {
            // Add tasks onto the job_manager so it can begin processing them
            while (!this->toSubmitJobs.empty()) 
            {
                auto to_submit = this->toSubmitJobs.front();
                auto job = std::get<0>(to_submit);      
                auto service_specific_args = std::get<1>(to_submit);
                job_manager->submitJob(job, batch_service, service_specific_args);
                this->toSubmitJobs.pop();
                std::printf("Submit Server Time: %f\n", this->simulation->getCurrentSimulatedDate());
            }

            // Clean up memory by removing completed and failed jobs
            while(not doneJobs.empty())
            {
                doneJobs.pop();
            }

            // Moves time forward for requested time while adding any completed events to a queue.
            // Needs to be done this way because waiting for next event cannot be done on another thread.
            while(this->simulation->getCurrentSimulatedDate() < server_time)
            {
                // Retrieve event
                auto event = this->waitForNextEvent(0.01);
                if(event != nullptr)
                {
                    std::printf("Event Server Time: %f\n", this->simulation->getCurrentSimulatedDate());
                    std::printf("Event: %s\n", event->toString().c_str());
                    queue_mutex.lock();
                    events.push(std::make_pair(this->simulation->getCurrentSimulatedDate(), event));
                    queue_mutex.unlock();
                }
            }

            // Exits if server needs to stop
            if(stop)
                break;
        }
        return 0;
    }

    void WorkflowManager::stopServer()
    {
        stop = true;
    }

    std::string WorkflowManager::addJob(const std::string& job_name, const double& duration,
                                  const unsigned int& num_nodes)
    {
        if(num_nodes > node_count)
            return "";
        // Create tasks and add to workflow.
        auto task = this->getWorkflow()->addTask(
            job_name + "_task_" + std::to_string(server_time), duration, 1, 1, 0.0);
        
        // Create a job
        auto job = job_manager->createStandardJob(task, {});

        std::map<std::string, std::string> service_specific_args;
        service_specific_args["-t"] = std::to_string(duration);
        service_specific_args["-N"] = std::to_string(num_nodes);
        service_specific_args["-c"] = std::to_string(1);
        service_specific_args["-u"] = "slurm_user";

        toSubmitJobs.push(std::make_pair(job, service_specific_args));
        job_list[job->getName()] = job;
        return job->getName();
    }

    bool WorkflowManager::cancelJob(const std::string& job_name)
    {
        if(job_list[job_name] != nullptr)
        {
            auto batch_service = *(this->getAvailableComputeServices<BatchComputeService>().begin());
            batch_service->terminateJob(job_list[job_name]);
            return true;
        }
        return false;
    }

    void WorkflowManager::getEventStatuses(std::queue<std::string>& statuses, const time_t& time)
    {
        // Keeps retrieving events while there are events and converts them to a string(temp) to return
        // to client.
        while(!events.empty())
        {
            queue_mutex.lock();
            auto event = events.front();
            statuses.push(std::to_string(event.first) + " " + event.second->toString());
            events.pop();
            queue_mutex.unlock();

            // Cleans up by pushing done/failed jobs onto a queue for main thread to clean up.
            if(auto failed = std::dynamic_pointer_cast<wrench::StandardJobFailedEvent>(event.second))
                doneJobs.push(failed->standard_job);
            else if(auto complete = std::dynamic_pointer_cast<wrench::StandardJobCompletedEvent>(event.second))
                doneJobs.push(complete->standard_job);
        }        
        server_time = time;
    }

    std::vector<std::tuple<std::string,std::string,int,int,int,double,double>> WorkflowManager::get_queue()
    {
        std::vector<std::tuple<std::string,std::string,int,int,int,double,double>> queue;
        auto batch_services = this->getAvailableComputeServices<BatchComputeService>();
        for(auto const bs : batch_services)
        {
            auto bs_queue = bs->getQueue();
            queue.insert(queue.end(), bs_queue.begin(), bs_queue.end());
        }
        for(auto const q : queue)
            std::cout << std::get<0>(q) << ' ' << std::get<1>(q) << std::endl;
        return queue;
    }
}

