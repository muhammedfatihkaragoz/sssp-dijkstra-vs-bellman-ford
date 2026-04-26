#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <limits>
#include <queue>
#include <chrono>
#include <string>
#include <iomanip>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace std;
using namespace std::chrono;

const long long INF = (numeric_limits<long long>::max)() / 4;

struct Edge
{
    int to;
    int weight;
};

typedef vector<vector<Edge>> Graph;

// ---------------- MEMORY ----------------
double getProcessMemoryMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;

    if (GetProcessMemoryInfo(
        GetCurrentProcess(),
        (PROCESS_MEMORY_COUNTERS*)&pmc,
        sizeof(pmc)))
    {
        return pmc.PrivateUsage / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
#ifdef __APPLE__
        return usage.ru_maxrss / (1024.0 * 1024.0);
#else
        return usage.ru_maxrss / 1024.0;
#endif
    }
    return 0.0;
#endif
}

// ---------------- DIMACS ----------------
bool readDIMACS(const string& filename, int& V, long long& E, Graph& adj)
{
    ifstream file(filename);

    if (!file.is_open())
    {
        cerr << "File could not be opened: " << filename << endl;
        return false;
    }

    string line;
    V = 0;
    E = 0;

    while (getline(file, line))
    {
        if (line.empty() || line[0] == 'c')
            continue;

        stringstream ss(line);
        char type;
        ss >> type;

        if (type == 'p')
        {
            string temp;
            ss >> temp >> V >> E;
            adj.assign(V + 1, vector<Edge>());
        }
        else if (type == 'a')
        {
            int u, v, w;
            ss >> u >> v >> w;

            if (u >= 1 && u <= V && v >= 1 && v <= V)
            {
                adj[u].push_back({ v, w });
            }
        }
    }

    file.close();
    return true;
}

// ---------------- GRAPH MEMORY ESTIMATION ----------------
double estimateGraphMemoryMB(int V, long long E)
{
    double edgeMem = E * sizeof(Edge);
    double vectorMem = (V + 1) * sizeof(vector<Edge>);
    return (edgeMem + vectorMem) / (1024.0 * 1024.0);
}

// ---------------- DIJKSTRA ----------------
long long runDijkstra(int start, int V, const Graph& adj, double& extraMemoryMB)
{
    double baseMem = getProcessMemoryMB();

    vector<long long> dist(V + 1, INF);

    priority_queue<
        pair<long long, int>,
        vector<pair<long long, int>>,
        greater<pair<long long, int>>
    > pq;

    auto t1 = high_resolution_clock::now();

    dist[start] = 0;
    pq.push({ 0, start });

    while (!pq.empty())
    {
        auto [d, u] = pq.top();
        pq.pop();

        if (d > dist[u])
            continue;

        for (const auto& e : adj[u])
        {
            if (dist[u] + e.weight < dist[e.to])
            {
                dist[e.to] = dist[u] + e.weight;
                pq.push({ dist[e.to], e.to });
            }
        }
    }

    double currentMem = getProcessMemoryMB();
    extraMemoryMB = currentMem - baseMem;

    if (extraMemoryMB < 0)
        extraMemoryMB = 0;

    auto t2 = high_resolution_clock::now();
    return duration_cast<milliseconds>(t2 - t1).count();
}

// ---------------- BELLMAN-FORD ----------------
bool runBellmanFord(int start, int V, const Graph& adj, long long& timeMs, double& extraMemoryMB)
{
    double baseMem = getProcessMemoryMB();

    vector<long long> dist(V + 1, INF);
    dist[start] = 0;

    auto t1 = high_resolution_clock::now();

    for (int i = 1; i <= V - 1; i++)
    {
        bool changed = false;

        for (int u = 1; u <= V; u++)
        {
            if (dist[u] == INF)
                continue;

            for (const auto& e : adj[u])
            {
                if (dist[u] + e.weight < dist[e.to])
                {
                    dist[e.to] = dist[u] + e.weight;
                    changed = true;
                }
            }
        }

        if (!changed)
            break;
    }

    bool negativeCycle = false;

    for (int u = 1; u <= V; u++)
    {
        if (dist[u] == INF)
            continue;

        for (const auto& e : adj[u])
        {
            if (dist[u] + e.weight < dist[e.to])
                negativeCycle = true;
        }
    }

    double currentMem = getProcessMemoryMB();
    extraMemoryMB = currentMem - baseMem;

    if (extraMemoryMB < 0)
        extraMemoryMB = 0;

    auto t2 = high_resolution_clock::now();
    timeMs = duration_cast<milliseconds>(t2 - t1).count();

    return negativeCycle;
}

// ---------------- MAIN ----------------
int main()
{
    vector<string> datasets = {
        "USA-road-d.NY-small.gr",
        "USA-road-d.CAL-small.gr",
        "USA-road-d.NY.gr",
        "USA-road-d.CAL.gr",
        "USA-road-d.USA.gr"
    };

    ofstream csv("results.csv");

    csv << "Dataset;Vertices;Edges;Algorithm;Runtime_ms;"
        << "GraphMemory_MB;AlgorithmExtraMemory_MB;TotalProcessMemory_MB;"
        << "Complexity;NegativeCycle;Status\n";

    for (const auto& ds : datasets)
    {
        int V;
        long long E;
        Graph adj;

        cout << "\nProcessing: " << ds << endl;

        if (!readDIMACS(ds, V, E, adj))
            continue;

        double graphMem = estimateGraphMemoryMB(V, E);
        double processMem = getProcessMemoryMB();

        int startNode = 1;

        // -------- DIJKSTRA --------
        double dijkstraExtraMem = 0.0;
        long long dTime = runDijkstra(startNode, V, adj, dijkstraExtraMem);
        processMem = getProcessMemoryMB();

        csv << ds << ";"
            << V << ";"
            << E << ";"
            << "Dijkstra;"
            << dTime << ";"
            << fixed << setprecision(2) << graphMem << ";"
            << fixed << setprecision(2) << dijkstraExtraMem << ";"
            << fixed << setprecision(2) << processMem << ";"
            << "O(E log V);"
            << "No;"
            << "COMPLETED\n";

        // -------- BELLMAN-FORD --------
        if (V < 50000)
        {
            long long bTime;
            double bellmanExtraMem = 0.0;
            bool neg = runBellmanFord(startNode, V, adj, bTime, bellmanExtraMem);

            processMem = getProcessMemoryMB();

            csv << ds << ";"
                << V << ";"
                << E << ";"
                << "Bellman-Ford;"
                << bTime << ";"
                << fixed << setprecision(2) << graphMem << ";"
                << fixed << setprecision(2) << bellmanExtraMem << ";"
                << fixed << setprecision(2) << processMem << ";"
                << "O(VE);"
                << (neg ? "Yes" : "No") << ";"
                << "COMPLETED\n";
        }
        else
        {
            csv << ds << ";"
                << V << ";"
                << E << ";"
                << "Bellman-Ford;"
                << "SKIPPED;"
                << fixed << setprecision(2) << graphMem << ";"
                << "-;"
                << fixed << setprecision(2) << processMem << ";"
                << "O(VE);"
                << "-;"
                << "SKIPPED_TOO_LARGE\n";
        }
    }

    csv.close();

    cout << "\nDONE. results.csv created." << endl;
    return 0;
}