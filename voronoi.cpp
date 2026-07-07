#include <assert.h>
#include <float.h>
#include <math.h>
#include <queue>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <functional>

using namespace std;

#include "helper.h"
#include "cells.h"

enum class BeachlineItemType
{
    None,
    Arc,
    Edge
};

struct SweepEvent;

struct Edge
{
    Vector2 start;
    Vector2 direction;
    bool extendsUpwardsForever;

    int siteIndexA;
    int siteIndexB;
};

struct Arc
{
    Vector2 focus;
    SweepEvent *squeezeEvent;

    int siteIndex;
};

struct BeachlineItem
{
    BeachlineItemType type;
    union
    {
        Arc arc;
        Edge edge;
    };

    BeachlineItem *parent;
    BeachlineItem *left;
    BeachlineItem *right;

    BeachlineItem() : parent(nullptr), left(nullptr), right(nullptr) {}

    void SetLeft(BeachlineItem *newLeft)
    {
        assert(type == BeachlineItemType::Edge);
        assert(newLeft != nullptr);
        left = newLeft;
        newLeft->parent = this;
    }

    void SetRight(BeachlineItem *newRight)
    {
        assert(type == BeachlineItemType::Edge);
        assert(newRight != nullptr);
        right = newRight;
        newRight->parent = this;
    }

    void SetParentFromItem(BeachlineItem *item)
    {
        assert(item != nullptr);
        if (item->parent == nullptr)
        {
            parent = nullptr;
            return;
        }

        if (item->parent->left == item)
        {
            item->parent->SetLeft(this);
        }
        else
        {
            assert(item->parent->right == item);
            item->parent->SetRight(this);
        }
    }
};

enum class SweepEventType
{
    None,
    NewPoint,
    EdgeIntersection
};

struct NewPointEvent
{
    Vector2 point;
    int siteIndex;
};

struct EdgeIntersectionEvent
{
    Vector2 intersectionPoint;
    BeachlineItem *squeezedArc;
    bool isValid;
};

struct SweepEvent
{
    float yCoord;
    SweepEventType type;
    union
    {
        NewPointEvent newPoint;
        EdgeIntersectionEvent edgeIntersect;
    };
};

#include "vtree.h"

struct FortuneState
{
    float sweepY;
    vector<CompleteEdge *> edges;
    vector<SweepEvent *> unencounteredEvents;
    BeachlineItem *beachlineRoot;
};

struct EventComparison
{
    bool operator()(const SweepEvent *lhs, const SweepEvent *rhs) const
    {
        assert((lhs != nullptr) && (rhs != nullptr));
        return lhs->yCoord < rhs->yCoord;
    }
};

static inline float GetArcYForXCoord(Arc &arc, float x, float directrixY)
{
    float a = 1.0f / (2.0f * (arc.focus.y - directrixY));
    float c = (arc.focus.y + directrixY) * 0.5f;

    float w = x - arc.focus.x;
    return a * w * w + c;
}

static inline bool GetEdgeArcIntersectionPoint(Edge &edge, Arc &arc, float directrixY, Vector2 &intersectionPt)
{
    // case 1: Edge is a vertical line.
    if (edge.direction.x == 0.0f)
    {
        if (directrixY == arc.focus.y)
        {
            // Arc is essentially a vertical line
            if (edge.start.x == arc.focus.x)
            {
                intersectionPt = arc.focus;
                return true;
            }
            else
            {
                return false;
            }
        }
        float arcY = GetArcYForXCoord(arc, edge.start.x, directrixY);
        intersectionPt = {edge.start.x, arcY};
        return true;
    }

    float p = edge.direction.y / edge.direction.x;
    float q = edge.start.y - p * edge.start.x;

    if (arc.focus.y == directrixY)
    {
        float intersectionXOffset = arc.focus.x - edge.start.x;
        if (intersectionXOffset * edge.direction.x < 0)
        {
            return false;
        }

        intersectionPt.x = arc.focus.x;
        intersectionPt.y = p * arc.focus.x + q;
        return true;
    }

    float a2 = 1.0f / (2.0f * (arc.focus.y - directrixY));
    float a1 = -p - 2.0f * a2 * arc.focus.x;
    float a0 = a2 * arc.focus.x * arc.focus.x + (arc.focus.y + directrixY) * 0.5f - q;

    float discriminant = a1 * a1 - 4.0f * a2 * a0;
    if (discriminant < 0)
    {
        return false;
    }
    float rootDisc = sqrtf(discriminant);
    float x1 = (-a1 + rootDisc) / (2.0f * a2);
    float x2 = (-a1 - rootDisc) / (2.0f * a2);

    float x1Offset = x1 - edge.start.x;
    float x2Offset = x2 - edge.start.x;
    float x1Dot = x1Offset * edge.direction.x;
    float x2Dot = x2Offset * edge.direction.x;

    float x;
    if ((x1Dot >= 0.0f) && (x2Dot < 0.0f))
        x = x1;
    else if ((x1Dot < 0.0f) && (x2Dot >= 0.0f))
        x = x2;
    else if ((x1Dot >= 0.0f) && (x2Dot >= 0.0f))
    {
        if (x1Dot < x2Dot)
            x = x1;
        else
            x = x2;
    }
    else
    {
        if (x1Dot < x2Dot)
            x = x2;
        else
            x = x1;
    }

    float y = GetArcYForXCoord(arc, x, directrixY);
    assert(isfinite(y));
    intersectionPt = {x, y};
    return true;
}

static inline BeachlineItem *GetActiveArcForXCoord(BeachlineItem *root, float x, float directrixY)
{
    BeachlineItem *currentItem = root;
    while (currentItem->type != BeachlineItemType::Arc)
    {
        assert(currentItem->type == BeachlineItemType::Edge);
        BeachlineItem *left = GetFirstLeafOnTheLeft(currentItem);
        BeachlineItem *right = GetFirstLeafOnTheRight(currentItem);
        assert((left != nullptr) && (left->type == BeachlineItemType::Arc));
        assert((right != nullptr) && (right->type == BeachlineItemType::Arc));

        BeachlineItem *fromLeft = GetFirstParentOnTheRight(left);
        BeachlineItem *fromRight = GetFirstParentOnTheLeft(right);
        assert((fromLeft != nullptr) && (fromLeft == fromRight));
        assert(fromLeft->type == BeachlineItemType::Edge);
        Edge &separatingEdge = fromLeft->edge;

        Vector2 leftIntersect;
        Vector2 rightIntersect;
        bool didLeftIntersect = GetEdgeArcIntersectionPoint(separatingEdge, left->arc, directrixY, leftIntersect);
        bool didRightIntersect = GetEdgeArcIntersectionPoint(separatingEdge, right->arc, directrixY, rightIntersect);

        float intersectionX = leftIntersect.x;
        if (!didLeftIntersect && didRightIntersect)
        {
            intersectionX = rightIntersect.x;
        }

        if (x < intersectionX)
        {
            currentItem = currentItem->left;
        }
        else
        {
            currentItem = currentItem->right;
        }
    }

    assert(currentItem->type == BeachlineItemType::Arc);
    return currentItem;
}

static inline BeachlineItem *CreateArc(Vector2 focus, int siteIndex)
{
    BeachlineItem *result = new BeachlineItem();
    result->type = BeachlineItemType::Arc;
    result->arc.focus = focus;
    result->arc.squeezeEvent = nullptr;
    result->arc.siteIndex = siteIndex;
    return result;
}

static inline BeachlineItem *CreateEdge(Vector2 start, Vector2 dir, int siteIndexA, int siteIndexB)
{
    BeachlineItem *result = new BeachlineItem();
    result->type = BeachlineItemType::Edge;
    result->edge.start = start;
    result->edge.direction = dir;
    result->edge.extendsUpwardsForever = false;
    result->edge.siteIndexA = siteIndexA;
    result->edge.siteIndexB = siteIndexB;
    return result;
}

static inline bool TryGetEdgeIntersectionPoint(Edge &e1, Edge &e2, Vector2 &intersectionPt)
{
    float dx = e2.start.x - e1.start.x;
    float dy = e2.start.y - e1.start.y;
    float det = e2.direction.x * e1.direction.y - e2.direction.y * e1.direction.x;
    float u = (dy * e2.direction.x - dx * e2.direction.y) / det;
    float v = (dy * e1.direction.x - dx * e1.direction.y) / det;

    if ((u < 0.0f) && !e1.extendsUpwardsForever)
        return false;
    if ((v < 0.0f) && !e2.extendsUpwardsForever)
        return false;
    if ((u == 0.0f) && (v == 0.0f) && !e1.extendsUpwardsForever && !e2.extendsUpwardsForever)
        return false;

    intersectionPt = {e1.start.x + e1.direction.x * u, e1.start.y + e1.direction.y * u};
    return true;
}

static inline void AddArcSqueezeEvent(
    priority_queue<SweepEvent *, vector<SweepEvent *>, EventComparison> &eventQueue,
    BeachlineItem *arc)
{
    BeachlineItem *leftEdge = GetFirstParentOnTheLeft(arc);
    BeachlineItem *rightEdge = GetFirstParentOnTheRight(arc);

    if ((leftEdge == nullptr) || (rightEdge == nullptr))
    {
        return;
    }

    Vector2 circleEventPoint;
    bool edgesIntersect = TryGetEdgeIntersectionPoint(leftEdge->edge, rightEdge->edge, circleEventPoint);
    if (!edgesIntersect)
    {
        return;
    }

    Vector2 circleCentreOffset = {arc->arc.focus.x - circleEventPoint.x,
                                  arc->arc.focus.y - circleEventPoint.y};
    float circleRadius = Magnitude(circleCentreOffset);
    float circleEventY = circleEventPoint.y - circleRadius;
    assert(arc->type == BeachlineItemType::Arc);

    if (arc->arc.squeezeEvent != nullptr)
    {
        if (arc->arc.squeezeEvent->yCoord >= circleEventY)
        {
            return;
        }
        else
        {
            assert(arc->arc.squeezeEvent->type == SweepEventType::EdgeIntersection);
            arc->arc.squeezeEvent->edgeIntersect.isValid = false;
        }
    }

    SweepEvent *newEvt = new SweepEvent();
    newEvt->type = SweepEventType::EdgeIntersection;
    newEvt->yCoord = circleEventY;
    newEvt->edgeIntersect.squeezedArc = arc;
    newEvt->edgeIntersect.intersectionPoint = circleEventPoint;
    newEvt->edgeIntersect.isValid = true;
    eventQueue.push(newEvt);

    arc->arc.squeezeEvent = newEvt;
}

static inline BeachlineItem *AddArcToBeachline(
    priority_queue<SweepEvent *, vector<SweepEvent *>, EventComparison> &eventQueue,
    BeachlineItem *root,
    SweepEvent &evt,
    float sweepLineY)
{
    Vector2 newPoint = evt.newPoint.point;
    int newSiteIndex = evt.newPoint.siteIndex;

    BeachlineItem *replacedArc = GetActiveArcForXCoord(root, newPoint.x, sweepLineY);
    assert((replacedArc != nullptr) && (replacedArc->type == BeachlineItemType::Arc));

    BeachlineItem *splitArcLeft = CreateArc(replacedArc->arc.focus, replacedArc->arc.siteIndex);
    BeachlineItem *splitArcRight = CreateArc(replacedArc->arc.focus, replacedArc->arc.siteIndex);
    BeachlineItem *newArc = CreateArc(newPoint, newSiteIndex);

    float intersectionY = GetArcYForXCoord(replacedArc->arc, newPoint.x, sweepLineY);
    assert(isfinite(intersectionY));
    Vector2 edgeStart = {newPoint.x, intersectionY};
    Vector2 focusOffset = {newArc->arc.focus.x - replacedArc->arc.focus.x,
                           newArc->arc.focus.y - replacedArc->arc.focus.y};
    Vector2 edgeDir = normalize({focusOffset.y, -focusOffset.x});

    int siteA = replacedArc->arc.siteIndex;
    int siteB = newArc->arc.siteIndex;

    BeachlineItem *edgeLeft = CreateEdge(edgeStart, edgeDir, siteA, siteB);
    BeachlineItem *edgeRight = CreateEdge(edgeStart, {-edgeDir.x, -edgeDir.y}, siteA, siteB);

    assert(replacedArc->left == nullptr);
    assert(replacedArc->right == nullptr);
    edgeLeft->SetParentFromItem(replacedArc);
    edgeLeft->SetLeft(splitArcLeft);
    edgeLeft->SetRight(edgeRight);
    edgeRight->SetLeft(newArc);
    edgeRight->SetRight(splitArcRight);

    BeachlineItem *newRoot = root;
    if (root == replacedArc)
    {
        newRoot = edgeLeft;
    }
    if (replacedArc->arc.squeezeEvent != nullptr)
    {
        assert(replacedArc->arc.squeezeEvent->type == SweepEventType::EdgeIntersection);
        assert(replacedArc->arc.squeezeEvent->edgeIntersect.isValid);
        replacedArc->arc.squeezeEvent->edgeIntersect.isValid = false;
    }
    VerifyThatThereAreNoReferencesToItem(newRoot, replacedArc);
    delete replacedArc;

    AddArcSqueezeEvent(eventQueue, splitArcLeft);
    AddArcSqueezeEvent(eventQueue, splitArcRight);

    return newRoot;
}

static inline BeachlineItem *RemoveArcFromBeachline(
    priority_queue<SweepEvent *, vector<SweepEvent *>, EventComparison> &eventQueue,
    BeachlineItem *root,
    vector<CompleteEdge *> &outputEdges,
    SweepEvent &evt)
{
    BeachlineItem *squeezedArc = evt.edgeIntersect.squeezedArc;
    assert(evt.type == SweepEventType::EdgeIntersection);
    assert(evt.edgeIntersect.isValid);
    assert(squeezedArc->arc.squeezeEvent == &evt);

    BeachlineItem *leftEdge = GetFirstParentOnTheLeft(squeezedArc);
    BeachlineItem *rightEdge = GetFirstParentOnTheRight(squeezedArc);
    assert((leftEdge != nullptr) && (rightEdge != nullptr));

    BeachlineItem *leftArc = GetFirstLeafOnTheLeft(leftEdge);
    BeachlineItem *rightArc = GetFirstLeafOnTheRight(rightEdge);
    assert((leftArc != nullptr) && (rightArc != nullptr));
    assert(leftArc != rightArc);

    Vector2 circleCentre = evt.edgeIntersect.intersectionPoint;

    CompleteEdge *edgeA = new CompleteEdge();
    edgeA->endpointA = leftEdge->edge.start;
    edgeA->endpointB = circleCentre;
    edgeA->siteIndexA = leftEdge->edge.siteIndexA;
    edgeA->siteIndexB = leftEdge->edge.siteIndexB;

    CompleteEdge *edgeB = new CompleteEdge();
    edgeB->endpointA = circleCentre;
    edgeB->endpointB = rightEdge->edge.start;
    edgeB->siteIndexA = rightEdge->edge.siteIndexA;
    edgeB->siteIndexB = rightEdge->edge.siteIndexB;

    if (leftEdge->edge.extendsUpwardsForever)
    {
        edgeA->endpointA.y = FLT_MAX;
    }
    if (rightEdge->edge.extendsUpwardsForever)
    {
        edgeB->endpointA.y = FLT_MAX;
    }
    outputEdges.emplace_back(edgeA);
    outputEdges.emplace_back(edgeB);

    Vector2 adjacentArcOffset = {};
    adjacentArcOffset.x = rightArc->arc.focus.x - leftArc->arc.focus.x;
    adjacentArcOffset.y = rightArc->arc.focus.y - leftArc->arc.focus.y;
    Vector2 newEdgeDirection = {adjacentArcOffset.y, -adjacentArcOffset.x};
    newEdgeDirection = normalize(newEdgeDirection);

    int siteA = leftArc->arc.siteIndex;
    int siteB = rightArc->arc.siteIndex;

    BeachlineItem *newItem = CreateEdge(circleCentre, newEdgeDirection, siteA, siteB);

    BeachlineItem *higherEdge = nullptr;
    BeachlineItem *tempItem = squeezedArc;
    while (tempItem->parent != nullptr)
    {
        tempItem = tempItem->parent;
        if (tempItem == leftEdge)
            higherEdge = leftEdge;
        if (tempItem == rightEdge)
            higherEdge = rightEdge;
    }
    assert((higherEdge != nullptr) && (higherEdge->type == BeachlineItemType::Edge));

    newItem->SetParentFromItem(higherEdge);
    newItem->SetLeft(higherEdge->left);
    newItem->SetRight(higherEdge->right);

    assert((squeezedArc->parent == nullptr) || (squeezedArc->parent->type == BeachlineItemType::Edge));
    BeachlineItem *remainingItem = nullptr;
    BeachlineItem *parent = squeezedArc->parent;
    if (parent->left == squeezedArc)
    {
        remainingItem = parent->right;
    }
    else
    {
        assert(parent->right == squeezedArc);
        remainingItem = parent->left;
    }
    assert((parent == leftEdge) || (parent == rightEdge));
    assert(parent != higherEdge);

    remainingItem->SetParentFromItem(parent);

    BeachlineItem *newRoot = root;
    if ((root == leftEdge) || (root == rightEdge))
    {
        newRoot = newItem;
    }
    VerifyThatThereAreNoReferencesToItem(newRoot, leftEdge);
    VerifyThatThereAreNoReferencesToItem(newRoot, squeezedArc);
    VerifyThatThereAreNoReferencesToItem(newRoot, rightEdge);
    if (squeezedArc->arc.squeezeEvent != nullptr)
    {
        squeezedArc->arc.squeezeEvent->edgeIntersect.isValid = false;
    }
    delete leftEdge;
    delete squeezedArc;
    delete rightEdge;

    AddArcSqueezeEvent(eventQueue, leftArc);
    AddArcSqueezeEvent(eventQueue, rightArc);
    return newRoot;
}

static inline void FinishEdge(BeachlineItem *item, vector<CompleteEdge *> &edges)
{
    if (item == nullptr)
    {
        return;
    }

    if (item->type == BeachlineItemType::Edge)
    {
        float length = 10000;
        Vector2 edgeEnd = item->edge.start;
        edgeEnd.x += length * item->edge.direction.x;
        edgeEnd.y += length * item->edge.direction.y;

        CompleteEdge *edge = new CompleteEdge();
        edge->endpointA = item->edge.start;
        edge->endpointB = edgeEnd;
        edge->siteIndexA = item->edge.siteIndexA;
        edge->siteIndexB = item->edge.siteIndexB;
        edges.emplace_back(edge);

        FinishEdge(item->left, edges);
        FinishEdge(item->right, edges);
    }

    delete item;
}

FortuneState FortunesAlgorithm(vector<Vector2> &sites, float cutoffY)
{
    vector<CompleteEdge *> edges;
    priority_queue<SweepEvent *, vector<SweepEvent *>, EventComparison> eventQueue;

    for (int i = 0; i < (int)sites.size(); ++i)
    {
        Vector2 pt = sites[i];
        SweepEvent *evt = new SweepEvent();
        evt->type = SweepEventType::NewPoint;
        evt->newPoint.point = pt;
        evt->newPoint.siteIndex = i; // NEW
        evt->yCoord = pt.y;
        eventQueue.push(evt);
    }

    SweepEvent *firstEvent = eventQueue.top();
    assert(firstEvent->type == SweepEventType::NewPoint);

    if (firstEvent->yCoord < cutoffY)
    {
        FortuneState result = {};
        result.sweepY = cutoffY;
        while (!eventQueue.empty())
        {
            result.unencounteredEvents.emplace_back(eventQueue.top());
            eventQueue.pop();
        }
        return result;
    }
    eventQueue.pop();

    BeachlineItem *firstArc = new BeachlineItem();
    firstArc->type = BeachlineItemType::Arc;
    firstArc->arc.focus = firstEvent->newPoint.point;
    firstArc->arc.squeezeEvent = nullptr;
    firstArc->arc.siteIndex = firstEvent->newPoint.siteIndex; // NEW
    delete firstEvent;
    BeachlineItem *root = firstArc;

    float startupSpecialCaseEndY = firstArc->arc.focus.y - 1.0f;
    while (!eventQueue.empty() && (eventQueue.top()->yCoord > startupSpecialCaseEndY))
    {
        SweepEvent *evt = eventQueue.top();
        if (evt->yCoord < cutoffY)
            break;
        eventQueue.pop();

        assert(evt->type == SweepEventType::NewPoint);
        Vector2 newFocus = evt->newPoint.point;
        int newSiteIndex = evt->newPoint.siteIndex;

        BeachlineItem *newArc = CreateArc(newFocus, newSiteIndex);

        BeachlineItem *activeArc = GetActiveArcForXCoord(root, newFocus.x, newFocus.y);
        assert(activeArc->type == BeachlineItemType::Arc);

        Vector2 edgeStart = {(newFocus.x + activeArc->arc.focus.x) / 2.0f,
                             newFocus.y + 100.0f};
        Vector2 edgeDir = {0.0f, -1.0f};

        BeachlineItem *newEdge = CreateEdge(edgeStart,
                                            edgeDir,
                                            activeArc->arc.siteIndex,
                                            newArc->arc.siteIndex);
        newEdge->edge.extendsUpwardsForever = true;

        if (activeArc->parent != nullptr)
        {
            if (activeArc == activeArc->parent->left)
            {
                activeArc->parent->SetLeft(newEdge);
            }
            else
            {
                assert(activeArc == activeArc->parent->right);
                activeArc->parent->SetRight(newEdge);
            }
        }
        else
        {
            root = newEdge;
        }
        if (newFocus.x < activeArc->arc.focus.x)
        {
            newEdge->SetLeft(newArc);
            newEdge->SetRight(activeArc);
        }
        else
        {
            newEdge->SetLeft(activeArc);
            newEdge->SetRight(newArc);
        }

        delete evt;
    }

    while (!eventQueue.empty())
    {
        SweepEvent *nextEvent = eventQueue.top();
        if (nextEvent->yCoord < cutoffY)
            break;
        eventQueue.pop();

        float sweepY = nextEvent->yCoord;
        if (nextEvent->type == SweepEventType::NewPoint)
        {
            root = AddArcToBeachline(eventQueue, root, *nextEvent, sweepY);
        }
        else if (nextEvent->type == SweepEventType::EdgeIntersection)
        {
            if (nextEvent->edgeIntersect.isValid)
            {
                root = RemoveArcFromBeachline(eventQueue, root, edges, *nextEvent);
            }
        }
        else
        {
            printf("Unrecognized queue item type: %d\n", nextEvent->type);
        }

        delete nextEvent;
    }
    if (eventQueue.empty() || (cutoffY < -200.0f))
    {
        FinishEdge(root, edges);
        root = nullptr;
    }

    FortuneState result;
    result.sweepY = 0.0f;
    result.beachlineRoot = root;
    result.edges = edges;
    while (!eventQueue.empty())
    {
        result.unencounteredEvents.emplace_back(eventQueue.top());
        eventQueue.pop();
    }
    return result;
}

void euclideanDistance(vector<Vector2> &sites, Vector2 &a)
{
    int siteIndex = -1;
    float minDistance = FLT_MAX;
    for (int i = 0; i < (int)sites.size(); ++i)
    {
        Vector2 site = sites[i];
        float dist = sqrtf((a.x - site.x) * (a.x - site.x) +
                           (a.y - site.y) * (a.y - site.y));
        if (dist < minDistance)
        {
            minDistance = dist;
            siteIndex = i;
        }
    }
    printf("Point at (%f,%f) → nearest site at (%f,%f)\n",
           a.x, a.y,
           sites[siteIndex].x, sites[siteIndex].y);
}

int main()
{
    vector<Vector2> sites = {
        {1.0f, 1.0f},
        {5.0f, 1.0f},
        {3.0f, 4.0f},
        {7.0f, 3.0f},
        {2.0f, 6.0f}};

    FortuneState fs = FortunesAlgorithm(sites, -FLT_MAX);

    vector<VoronoiCell> cells =
        BuildVoronoiCells((int)sites.size(), fs.edges);

    /*  printf(" Voronoi Edges (CompleteEdge list) \n");
       for (int i = 0; i < (int)fs.edges.size(); ++i)
       {
           CompleteEdge *e = fs.edges[i];
           printf("Edge %d: (%f, %f) -> (%f, %f)  |  sites (%d, %d)\n",
                  i,
                  e->endpointA.x, e->endpointA.y,
                  e->endpointB.x, e->endpointB.y,
                  e->siteIndexA, e->siteIndexB);
       }
       printf("\n");
   */

    vector<Vector2> testcases = {
        {2.0f, 2.0f},
        {6.0f, 1.0f},
        {4.0f, 5.0f},
        {1.0f, 5.0f}};

    int startCell = 0; // starting guess for Voronoi cell

    printf("Nearest-neighbors ->\n");
    for (int i = 0; i < (int)testcases.size(); ++i)
    {
        Vector2 q = testcases[i];
        int cellIndex = LocateVoronoiCell(cells, sites, startCell, q); // finding the Voronoi cell containing q
        startCell = cellIndex;                                         // store starting point for next testcase (faster implementation)
        printf("Point %d at (%f,%f) → nearest site at (%f,%f)\n",
               i + 1, q.x, q.y,
               sites[cellIndex].x, sites[cellIndex].y);
    }

    printf("\nUsing Euclidean distance for verification ->\n");
    for (int i = 0; i < (int)testcases.size(); ++i)
    {
        Vector2 q = testcases[i];
        euclideanDistance(sites, q);
    }

    return 0;
}
