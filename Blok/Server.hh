/* Server.hh - implementation/definition of the server protocol
 *
 * The server is basically the game engine, where the client requests to change something
 *  or retrieve information about the game state. Since 'Server' is an abstract class, this means
 *  clients/mods/etc can treat a local server the same as a networked one.
 *
 * The biggest problem is with networked servers, there will be a slight delay, so if a client requests 
 *  a chunk over a network, it definitely will NOT be available for the current frame. In that case, it
 *  was a valid request, and the data is present, but not currently available. In that case, the getChunk()
 *  method will return 'NULL'. So, the client/mods/etc should always check whether the call returned a valid
 *  Chunk, and not NULL
 *
 */

#pragma once

#ifndef BLOK_SERVER_HH__
#define BLOK_SERVER_HH__

// general Blok library
#include <Blok/Blok.hh>

// we use the WG protocol for generating worlds
#include <Blok/WG.hh>

// for MP processing
#include <mutex> 
#include <thread>
#include <time.h>



namespace Blok {

    // Server - an abstract class describing the server/game engine protocol,
    //   for management & gameplay
    // Most requests are performed async, but without handles, so that they put in a 'request',
    //   and during free cycles, the server will internally update itself using 
    class Server {
        public:

        // this mutex controls access to all chunk request variables
        std::mutex L_chunks;

        // a set (i.e. no duplicates) of active chunk requests
        Set<ChunkID> chunkRequests;

        // the set of currently being worked on in a background thread
        Set<ChunkID> chunkRequestsInProgress;

        // set of all chunks that are currently loaded by the server
        Map<ChunkID, Chunk*> loadedChunks;

        // If the chunk is currently loaded, just return a pointer to that chunk, which can be modified (see Blok.hh)
        // If it is not loaded, the behaviour depends on the 'request' parameter
        //   * If `request==true`, then the server will be notified that the chunk is being requested,
        //       and will attempt to, in the future, load/generate it, and make it available for subsequent calls
        //
        // NOTE: The caller should never delete a returned chunk; the server does its own memory management,
        //   and will free the chunk once the server object is deleted
        virtual Chunk* getChunk(ChunkID id, bool request=true) {
            // enter critical section, we are accessing variables
            L_chunks.lock();
            auto seq = loadedChunks.find(id);
            // by default, we didn't find it
            Chunk* ret = NULL;
            if (seq == loadedChunks.end()) {
                // not found
                if (request) {
                    // add to the requests
                    // make sure it is not currently being requested
                    if (chunkRequests.find(id) == chunkRequests.end() && chunkRequestsInProgress.find(id) == chunkRequestsInProgress.end()) {
                        chunkRequests.insert(id);
                    }
                }
            } else {
                // found, so just return it
                ret = seq->second;
            }

            L_chunks.unlock();
            return ret;
        }

        // get a chunk from the server, if it is already loaded. This will not attempt to load/generate the chunk
        // else, return NULL
        //virtual Chunk* getChunkIfLoaded(ChunkID id) = 0;

        // this virtual function allows the server to process chunk requests (i.e. load in chunks from a data-base,
        //   generate them, etc) for a maximum amount of time
        // it should return the actual number of requests processed
        //virtual int processChunkRequests(double maxTime) = 0;

        // attempt to cast a ray (in world space), up to 'dist', returning whether or not it hit something
        // In the case that it did hit something, also set `hitInfo` to the relevant data about the collision
        // See `Blok.hh`, specifically around `struct RayHit` for more information
        virtual bool raycastBlock(Ray ray, float dist, RayHit& hitInfo) = 0;

    };

    // LocalServer - a server implementation that operates locally (i.e. on the current machine,
    //   not over a network)
    // NOTE: This is also the server running on hosted servers, but it simply allows for managed network connections
    //   to interact and request things rather than clients
    class LocalServer : public Server {

        public:

        // a structure describing statistics of performance
        struct {

            // the number of chunks generated
            int n_chunks;

            // the total time spent generating chunks
            double t_chunks;

        } stats;


        // the world generator that is currently being used to generate chunks
        WG::WG* worldGen;

        // thread to run the chunk loading
        std::thread T_chunkLoad;



        // construct a new local server.
        // For now, just create a default world generator
        LocalServer() {
            worldGen = new WG::DefaultWG(0);
            //worldGen = new WG::FlatWG(0);

            // start the thread
            T_chunkLoad = std::thread(&LocalServer::T_chunkLoad_run, this);

            // initialize statistics
            stats.n_chunks = 0;
            stats.t_chunks = 0.0;
        }

        // destroy server & its resources
        ~LocalServer() {
            // remove our generator
            delete worldGen;


            // delete all loaded chunks
            for (auto& entry : loadedChunks) {
                delete entry.second;
            }

        }

        // raycast() should seek through all possible chunks, checking intersection along 'ray',
        //   up to 'maxDist'. If it ends up hitting a solid block, return true and set all the 'to*'
        //   arguments to the data about the hit
        bool raycastBlock(Ray ray, float dist, RayHit& hitInfo);

        private:
        /* internal methods */

        // this is the target that should be ran all the time, by the T_chunkLoad thread,
        //   which attempts to service the chunk loader
        void T_chunkLoad_run() {
            while (true) {
                struct timespec tim, tim2;
                // wait for a second
                tim.tv_sec = 0;
                // run every 100 ms
                tim.tv_nsec = 100 * 1000;
                // wait until there was something
                while (chunkRequests.size() == 0) {
                    nanosleep(&tim, &tim2);
                }

                L_chunks.lock();
                // now, grab them off
                chunkRequestsInProgress = chunkRequests;
                // say we are handling all of them
                chunkRequests.clear();
                L_chunks.unlock();

                // our results,
                Map<ChunkID, Chunk*> res;

                /* actual chunk generation */

                for (auto cid : chunkRequestsInProgress) {
                    // calculate the chunk
                    res[cid] = worldGen->getChunk(cid);
                }

                // store them back

                L_chunks.lock();
                for (auto elem : res) {
                    loadedChunks[elem.first] = elem.second;
                }
                chunkRequestsInProgress.clear();
                L_chunks.unlock();

            }

        }

    };

}


#endif /* BLOK_WG_HH__ */
