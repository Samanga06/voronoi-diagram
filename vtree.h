

#include <assert.h>

static inline BeachlineItem *GetFirstParentOnTheLeft(BeachlineItem *item)
{
    BeachlineItem *current = item;
    while ((current->parent != nullptr) && (current->parent->left == current))
    {
        current = current->parent;
    }
    assert((current->parent == nullptr) || (current->parent->type == BeachlineItemType::Edge));
    return current->parent;
}

static inline BeachlineItem *GetFirstParentOnTheRight(BeachlineItem *item)
{
    BeachlineItem *current = item;
    while ((current->parent != nullptr) && (current->parent->right == current))
    {
        current = current->parent;
    }
    assert((current->parent == nullptr) || (current->parent->type == BeachlineItemType::Edge));
    return current->parent;
}

static inline BeachlineItem *GetFirstLeafOnTheLeft(BeachlineItem *item)
{
    if (item->left == nullptr)
    {
        return nullptr;
    }
    BeachlineItem *current = item->left;
    while (current->right != nullptr)
    {
        current = current->right;
    }
    assert(current->type == BeachlineItemType::Arc);
    return current;
}

static inline BeachlineItem *GetFirstLeafOnTheRight(BeachlineItem *item)
{
    if (item->right == nullptr)
    {
        return nullptr;
    }
    BeachlineItem *current = item->right;
    while (current->left != nullptr)
    {
        current = current->left;
    }
    assert(current->type == BeachlineItemType::Arc);
    return current;
}

static inline void DeleteBeachlineItem(BeachlineItem *item)
{
    if (item == nullptr)
        return;
    DeleteBeachlineItem(item->left);
    DeleteBeachlineItem(item->right);
    delete item;
}

static inline int CountBeachlineItems(BeachlineItem *root)
{
    if (root == nullptr)
        return 0;
    int left = CountBeachlineItems(root->left);
    int right = CountBeachlineItems(root->right);
    return left + right + 1;
}

static inline void VerifyThatThereAreNoReferencesToItem(BeachlineItem *root, BeachlineItem *item)
{
    if (root == nullptr)
        return;
    if (root->type == BeachlineItemType::Arc)
        return;

    assert(root->parent != item);
    assert(root->left != item);
    assert(root->right != item);

    VerifyThatThereAreNoReferencesToItem(root->left, item);
    VerifyThatThereAreNoReferencesToItem(root->right, item);
}
