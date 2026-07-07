#pragma once

#include <vector>
#include <algorithm>
#include "helper.h"

struct CompleteEdge
{
    Vector2 endpointA;
    Vector2 endpointB;

    int siteIndexA;
    int siteIndexB;
};

struct VoronoiCell
{
    int siteIndex;
    std::vector<int> edges;
    std::vector<int> neighbors;
};

static inline std::vector<VoronoiCell> BuildVoronoiCells(
    int numSites,
    const std::vector<CompleteEdge *> &edges)
{
    std::vector<VoronoiCell> cells(numSites);
    for (int i = 0; i < numSites; ++i)
    {
        cells[i].siteIndex = i;
    }

    for (int ei = 0; ei < (int)edges.size(); ++ei)
    {
        const CompleteEdge *e = edges[ei];
        int a = e->siteIndexA;
        int b = e->siteIndexB;

        if (a < 0 || b < 0)
            continue;

        cells[a].edges.push_back(ei);
        cells[b].edges.push_back(ei);

        cells[a].neighbors.push_back(b);
        cells[b].neighbors.push_back(a);
    }

    // Deduplicate neighbors
    for (int i = 0; i < numSites; ++i)
    {
        auto &n = cells[i].neighbors;
        std::sort(n.begin(), n.end());
        n.erase(std::unique(n.begin(), n.end()), n.end());
    }

    return cells;
}

static inline int LocateVoronoiCell(
    const std::vector<VoronoiCell> &cells,
    const std::vector<Vector2> &sites,
    int startCellIndex,
    const Vector2 &p)
{
    int current = startCellIndex;

    while (true)
    {
        const VoronoiCell &cell = cells[current];
        int bestCell = current;
        float bestDist2 = SquaredDistance(p, sites[cell.siteIndex]);

        for (int neighborSiteIdx : cell.neighbors)
        {
            const VoronoiCell &neighborCell = cells[neighborSiteIdx];
            float d2 = SquaredDistance(p, sites[neighborCell.siteIndex]);
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                bestCell = neighborSiteIdx;
            }
        }

        if (bestCell == current)
            return current;

        current = bestCell;
    }
}
