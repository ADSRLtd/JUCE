/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

//==============================================================================
UndoManager::UndoManager (int maxNumberOfUnitsToKeep, int minimumTransactions)
{
    setMaxNumberOfStoredUnits (maxNumberOfUnitsToKeep, minimumTransactions);
}

UndoManager::~UndoManager()
{
}

//==============================================================================
void UndoManager::clearUndoHistory()
{
    transactions.clear();
    totalUnitsStored = 0;
    nextIndex = 0;
    sendChangeMessage();
}

int UndoManager::getNumberOfUnitsTakenUpByStoredCommands() const
{
    return totalUnitsStored;
}

void UndoManager::setMaxNumberOfStoredUnits (int maxUnits, int minTransactions)
{
    maxNumUnitsToKeep          = jmax (1, maxUnits);
    minimumTransactionsToKeep  = jmax (1, minTransactions);
}

//==============================================================================
bool UndoManager::perform (UndoableAction* newAction, const String& actionName)
{
    if (perform (newAction))
    {
        if (actionName.isNotEmpty())
            setCurrentTransactionName (actionName);

        return true;
    }

    return false;
}

bool UndoManager::perform (UndoableAction* newAction)
{
    if (newAction != nullptr)
    {
        std::unique_ptr<UndoableAction> action (newAction);

        if (isPerformingUndoRedo())
        {
            jassertfalse;  // Don't call perform() recursively from the UndoableAction::perform()
                           // or undo() methods, or else these actions will be discarded!
            return false;
        }

        if (action->perform())
        {
            auto* actionSet = getCurrentSet();

            if (actionSet != nullptr && ! newTransaction)
            {
                if (auto* lastAction = actionSet->actions.getLast())
                {
                    if (auto coalescedAction = lastAction->createCoalescedAction (action.get()))
                    {
                        action.reset (coalescedAction);
                        totalUnitsStored -= lastAction->getSizeInUnits();
                        actionSet->actions.removeLast();
                    }
                }
            }
            else
            {
                actionSet = new ActionSet (newTransactionName);
                transactions.insert (nextIndex, actionSet);
                ++nextIndex;
            }

            totalUnitsStored += action->getSizeInUnits();
            actionSet->actions.add (std::move (action));
            newTransaction = false;

            moveFutureTransactionsToStash();
            dropOldTransactionsIfTooLarge();
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

void UndoManager::moveFutureTransactionsToStash()
{
    if (nextIndex < transactions.size())
    {
        stashedFutureTransactions.clear();

        while (nextIndex < transactions.size())
        {
            auto* removed = transactions.removeAndReturn (nextIndex);
            stashedFutureTransactions.add (removed);
            totalUnitsStored -= removed->getTotalSize();
        }
    }
}

void UndoManager::restoreStashedFutureTransactions()
{
    while (nextIndex < transactions.size())
    {
        totalUnitsStored -= transactions.getUnchecked (nextIndex)->getTotalSize();
        transactions.remove (nextIndex);
    }

    for (auto* stashed : stashedFutureTransactions)
    {
        transactions.add (stashed);
        totalUnitsStored += stashed->getTotalSize();
    }

    stashedFutureTransactions.clearQuick (false);
}

void UndoManager::dropOldTransactionsIfTooLarge()
{
    while (nextIndex > 0
            && totalUnitsStored > maxNumUnitsToKeep
            && transactions.size() > minimumTransactionsToKeep)
    {
        totalUnitsStored -= transactions.getFirst()->getTotalSize();
        transactions.remove (0);
        --nextIndex;

        // if this fails, then some actions may not be returning
        // consistent results from their getSizeInUnits() method
        jassert (totalUnitsStored >= 0);
    }
}

void UndoManager::beginNewTransaction()
{
    beginNewTransaction ({});
}

void UndoManager::beginNewTransaction (const String& actionName)
{
    newTransaction = true;
    newTransactionName = actionName;
}

void UndoManager::setCurrentTransactionName (const String& newName)
{
    if (newTransaction)
        newTransactionName = newName;
    else if (auto* action = getCurrentSet())
        action->name = newName;
}

String UndoManager::getCurrentTransactionName() const
{
    if (auto* action = getCurrentSet())
        return action->name;

    return newTransactionName;
}

//==============================================================================
ActionSet* UndoManager::getCurrentSet() const     { return transactions[nextIndex - 1]; }
ActionSet* UndoManager::getNextSet() const        { return transactions[nextIndex]; }

bool UndoManager::isCurrentTransaction(const ActionSet* transaction) const { return transaction == getCurrentSet(); }
bool UndoManager::isPerformingUndoRedo() const  { return isInsideUndoRedoCall; }

bool UndoManager::canUndo() const      { return getCurrentSet() != nullptr; }
bool UndoManager::canRedo() const      { return getNextSet()    != nullptr; }

bool UndoManager::undo()
{
    if (auto* s = getCurrentSet())
    {
        const ScopedValueSetter<bool> setter (isInsideUndoRedoCall, true);

        if (s->undo())
            --nextIndex;
        else
            clearUndoHistory();

        beginNewTransaction();
        sendChangeMessage();
        return true;
    }

    return false;
}

bool UndoManager::redo()
{
    if (auto* s = getNextSet())
    {
        const ScopedValueSetter<bool> setter (isInsideUndoRedoCall, true);

        if (s->perform())
            ++nextIndex;
        else
            clearUndoHistory();

        beginNewTransaction();
        sendChangeMessage();
        return true;
    }

    return false;
}

String UndoManager::getUndoDescription() const
{
    if (auto* s = getCurrentSet())
        return s->name;

    return {};
}

String UndoManager::getRedoDescription() const
{
    if (auto* s = getNextSet())
        return s->name;

    return {};
}

StringArray UndoManager::getUndoDescriptions() const
{
    StringArray descriptions;

    for (int i = nextIndex;;)
    {
        if (auto* t = transactions[--i])
            descriptions.add (t->name);
        else
            return descriptions;
    }
}

StringArray UndoManager::getRedoDescriptions() const
{
    StringArray descriptions;

    for (int i = nextIndex;;)
    {
        if (auto* t = transactions[i++])
            descriptions.add (t->name);
        else
            return descriptions;
    }
}

Time UndoManager::getTimeOfUndoTransaction() const
{
    if (auto* s = getCurrentSet())
        return s->time;

    return {};
}

Time UndoManager::getTimeOfRedoTransaction() const
{
    if (auto* s = getNextSet())
        return s->time;

    return Time::getCurrentTime();
}

bool UndoManager::undoCurrentTransactionOnly()
{
    if ((! newTransaction) && undo())
    {
        restoreStashedFutureTransactions();
        return true;
    }

    return false;
}

void UndoManager::getActionsInCurrentTransaction (Array<const UndoableAction*>& actionsFound) const
{
    if (! newTransaction)
        if (auto* s = getCurrentSet())
            for (auto* a : s->actions)
                actionsFound.add (a);
}

int UndoManager::getNumActionsInCurrentTransaction() const
{
    if (! newTransaction)
        if (auto* s = getCurrentSet())
            return s->actions.size();

    return 0;
}

std::vector<ActionSet*> UndoManager::getTransactions() const
{
    std::vector<ActionSet*> allTransactions;

    for(auto* actionSet : transactions)
    {
        allTransactions.push_back(actionSet);
    }
    return allTransactions;
}

} // namespace juce