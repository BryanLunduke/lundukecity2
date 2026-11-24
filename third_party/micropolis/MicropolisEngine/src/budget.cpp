/* budget.cpp
 *
 * Micropolis, Unix Version.  This game was released for the Unix platform
 * in or about 1990 and has been modified for inclusion in the One Laptop
 * Per Child program.  Copyright (C) 1989 - 2007 Electronic Arts Inc.  If
 * you need assistance with this program, you may contact:
 *   http://wiki.laptop.org/go/Micropolis  or email  micropolis@laptop.org.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.  You should have received a
 * copy of the GNU General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 *             ADDITIONAL TERMS per GNU GPL Section 7
 *
 * No trademark or publicity rights are granted.  This license does NOT
 * give you any right, title or interest in the trademark SimCity or any
 * other Electronic Arts trademark.  You may not distribute any
 * modification of this program using the trademark SimCity or claim any
 * affliation or association with Electronic Arts Inc. or its employees.
 *
 * Any propagation or conveyance of this program must include this
 * copyright notice and these terms.
 *
 * If you convey this program (or any modifications of it) and assume
 * contractual liability for the program to recipients of it, you agree
 * to indemnify Electronic Arts for any liability that those contractual
 * assumptions impose on Electronic Arts.
 *
 * You may not misrepresent the origins of this program; modified
 * versions of the program must be marked as such and not identified as
 * the original program.
 *
 * This disclaimer supplements the one included in the General Public
 * License.  TO THE FULLEST EXTENT PERMISSIBLE UNDER APPLICABLE LAW, THIS
 * PROGRAM IS PROVIDED TO YOU "AS IS," WITH ALL FAULTS, WITHOUT WARRANTY
 * OF ANY KIND, AND YOUR USE IS AT YOUR SOLE RISK.  THE ENTIRE RISK OF
 * SATISFACTORY QUALITY AND PERFORMANCE RESIDES WITH YOU.  ELECTRONIC ARTS
 * DISCLAIMS ANY AND ALL EXPRESS, IMPLIED OR STATUTORY WARRANTIES,
 * INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY, SATISFACTORY QUALITY,
 * FITNESS FOR A PARTICULAR PURPOSE, NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS, AND WARRANTIES (IF ANY) ARISING FROM A COURSE OF DEALING,
 * USAGE, OR TRADE PRACTICE.  ELECTRONIC ARTS DOES NOT WARRANT AGAINST
 * INTERFERENCE WITH YOUR ENJOYMENT OF THE PROGRAM; THAT THE PROGRAM WILL
 * MEET YOUR REQUIREMENTS; THAT OPERATION OF THE PROGRAM WILL BE
 * UNINTERRUPTED OR ERROR-FREE, OR THAT THE PROGRAM WILL BE COMPATIBLE
 * WITH THIRD PARTY SOFTWARE OR THAT ANY ERRORS IN THE PROGRAM WILL BE
 * CORRECTED.  NO ORAL OR WRITTEN ADVICE PROVIDED BY ELECTRONIC ARTS OR
 * ANY AUTHORIZED REPRESENTATIVE SHALL CREATE A WARRANTY.  SOME
 * JURISDICTIONS DO NOT ALLOW THE EXCLUSION OF OR LIMITATIONS ON IMPLIED
 * WARRANTIES OR THE LIMITATIONS ON THE APPLICABLE STATUTORY RIGHTS OF A
 * CONSUMER, SO SOME OR ALL OF THE ABOVE EXCLUSIONS AND LIMITATIONS MAY
 * NOT APPLY TO YOU.
 */

/** @file budget.cpp */

////////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include "micropolis.h"
#include "text.h"


////////////////////////////////////////////////////////////////////////


void Micropolis::initFundingLevel()
{
    firePercent = 1.0;
    fireValue = 0;
    policePercent = 1.0;
    policeValue = 0;
    roadPercent = 1.0;
    roadValue = 0;
    roadPercentWanted = 1.0f;
    firePercentWanted = 1.0f;
    policePercentWanted = 1.0f;
    autoBudgetForcedOff = false;
    mustDrawBudget = 1;
}


/**
 * Review nov19 #6: a short year used to leave police and fire at the cut
 * share for good, even with auto budget on and the treasury full again
 * (the cut percent became next year's request). With auto budget on, or
 * switched off by the engine for lack of money, each year asks for the
 * funding the player chose again once taxes plus cash can pay all of it.
 * If the engine had switched auto budget off, it comes back on and the
 * player is told.
 */
void Micropolis::restoreWantedFunding()
{
    if (!autoBudget && !autoBudgetForcedOff) {
        return;
    }
    const Quad want = (Quad)(roadFund * roadPercentWanted) + (Quad)(fireFund * firePercentWanted) +
                      (Quad)(policeFund * policePercentWanted);
    // Only once the treasury has recovered: this year's taxes plus cash
    // must cover two years of the full request, so a city that just
    // scraped through is not sent straight back to $0.
    if ((Quad)taxFund + (Quad)totalFunds < want * 2) {
        return;
    }
    roadPercent = roadPercentWanted;
    firePercent = firePercentWanted;
    policePercent = policePercentWanted;
    if (autoBudgetForcedOff) {
        autoBudgetForcedOff = false;
        setAutoBudget(true);
        sendMessage(MESSAGE_AUTO_BUDGET_RESTORED, NOWHERE, NOWHERE, false, true);
    }
}

/** Game decided to show the budget window */
void Micropolis::doBudget()
{
    doBudgetNow(false);
}


/** User queried the budget window */
void Micropolis::doBudgetFromMenu()
{
    doBudgetNow(true);
}


/**
 * Handle budget window.
 * @param fromMenu User requested the budget window.
 * @todo Simplify this code. Instead of this nested mess, make a sequence of
 *       assigning funds to road, fire, and police.
 */
namespace {

Quad take_budget_share(float &percent, Quad fund, Quad &available)
{
    if (fund <= 0 || percent <= 0.0f || available <= 0) {
        if (fund > 0 && available <= 0) {
            percent = 0.0f;
        }
        return 0;
    }
    if (percent > 1.0f) {
        percent = 1.0f;
    }
    Quad want = (Quad)((double)fund * (double)percent);
    if (want < 0) {
        want = 0;
    }
    if (want > available) {
        percent = (float)((double)available / (double)fund);
        if (percent < 0.0f) {
            percent = 0.0f;
        }
        if (percent > 1.0f) {
            percent = 1.0f;
        }
        want = (Quad)((double)fund * (double)percent);
        if (want > available) {
            want = available;
        }
    }
    available -= want;
    return want;
}

} // namespace


Micropolis::BudgetCharge Micropolis::budgetCharge() const
{
    // Same share as commitBudgetPayment, on copies of the percents, so the
    // budget window can show the transaction Close will post.
    BudgetCharge charge;
    charge.roadPercent = roadPercent;
    charge.firePercent = firePercent;
    charge.policePercent = policePercent;

    Quad available = (Quad)taxFund + (Quad)totalFunds;
    if (available < 0) {
        available = 0;
    }
    charge.roadTaken = take_budget_share(charge.roadPercent, roadFund, available);
    charge.fireTaken = take_budget_share(charge.firePercent, fireFund, available);
    charge.policeTaken = take_budget_share(charge.policePercent, policeFund, available);

    const Quad spent = charge.roadTaken + charge.fireTaken + charge.policeTaken;
    Quad moreDough = (Quad)taxFund - spent;
    if ((Quad)totalFunds + moreDough < 0) {
        moreDough = -(Quad)totalFunds;
    }
    if (moreDough > 2000000000L) {
        moreDough = 2000000000L;
    }
    if (moreDough < -2000000000L) {
        moreDough = -2000000000L;
    }
    charge.posted = moreDough;
    return charge;
}


void Micropolis::commitBudgetPayment()
{
    if (!budgetAwaitingAccept) {
        return;
    }
    budgetAwaitingAccept = false;

    // Sliders can rise after doBudgetNow scaled them. Charge only what
    // taxFund + cash on hand can cover, roads first, then fire, then police.
    const BudgetCharge charge = budgetCharge();
    roadPercent = charge.roadPercent;
    firePercent = charge.firePercent;
    policePercent = charge.policePercent;
    roadSpend = charge.roadTaken;
    fireSpend = charge.fireTaken;
    policeSpend = charge.policeTaken;
    roadValue = charge.roadTaken;
    fireValue = charge.fireTaken;
    policeValue = charge.policeTaken;
    spend((int)(-charge.posted));
    // Civic gifts (park, casino) pay with the year, after the tax charge
    // is fixed, so they do not change how much the departments can take.
    if (specialIncome > 0) {
        spend((int)(-specialIncome));
        specialIncome = 0;
    }
    updateFundEffects();
}


Quad Micropolis::taxGrowthPercent() const
{
    if (cityPop <= TAX_GROWTH_FROM_POPULATION) {
        return 0;
    }
    const Quad steps = (cityPop - TAX_GROWTH_FROM_POPULATION) / 1000;
    const Quad percent = steps * TAX_GROWTH_PERCENT_PER_THOUSAND;
    return percent > TAX_GROWTH_PERCENT_MAX ? TAX_GROWTH_PERCENT_MAX : percent;
}


Quad Micropolis::stationUpkeep(bool fire) const
{
    if (scenario == SC_LEARN_TO_PLAY) {
        return 25;
    }
    if (scenario == SC_PORTLAND && fire) {
        return 60;
    }
    return 100;
}


short Micropolis::scenarioStartTax(Scenario which)
{
    // Portland and San Diego are big cities that ran $250-$1,000 a year in
    // the red at 7% once their goal was settled (review nov24 #13).
    if (which == SC_PORTLAND || which == SC_SAN_DIEGO) {
        return 8;
    }
    return 7;
}


Micropolis::BudgetBases Micropolis::projectBudget(short roads, short rails, short policePop, short firePop, Quad pop,
                                                   short landValue, short taxRate) const
{
    // Same tables collectTax and recomputeTaxFund have always used.
    static const float RLevels[3] = { 0.7f, 0.9f, 1.2f };
    static const float FLevels[3] = { 1.4f, 1.2f, 0.8f };
    int level = (int)gameLevel;
    if (level < 0 || level >= LEVEL_COUNT) {
        level = 0;
    }
    if (pop < 0) {
        pop = 0;
    }
    if (landValue < 0) {
        landValue = 0;
    }
    if (taxRate < 0) {
        taxRate = 0;
    }
    if (roads < 0) {
        roads = 0;
    }
    if (rails < 0) {
        rails = 0;
    }
    if (policePop < 0) {
        policePop = 0;
    }
    if (firePop < 0) {
        firePop = 0;
    }

    BudgetBases out;
    out.police = (Quad)policePop * stationUpkeep(false);
    out.fire = (Quad)firePop * stationUpkeep(true);
    out.road = (Quad)((roads + (rails * 2)) * RLevels[level]);
    out.tax = (Quad)((((Quad)pop * (Quad)landValue) / 120) * (Quad)taxRate * FLevels[level]);
    // Review nov19 #5: upkeep grows with every road and station, but the
    // tax roll flattened out, so a big city could never afford a second
    // wave of building. A city past 10,000 people (the last evaluation's
    // cityPop) now collects 2% more per 1,000 people, at most 30% more.
    const Quad bonus = taxGrowthPercent();
    if (bonus > 0 && out.tax > 0) {
        out.tax += out.tax * bonus / 100;
    }
    if (out.tax < 0) {
        out.tax = 0;
    }
    return out;
}


void Micropolis::applyFundingLevels()
{
    auto share = [](Quad fund, float percent) -> Quad {
        if (fund <= 0 || percent <= 0.0f) {
            return 0;
        }
        if (percent > 1.0f) {
            percent = 1.0f;
        }
        Quad taken = (Quad)((double)fund * (double)percent);
        if (taken < 0) {
            taken = 0;
        }
        return taken;
    };
    roadSpend = share(roadFund, roadPercent);
    fireSpend = share(fireFund, firePercent);
    policeSpend = share(policeFund, policePercent);
    roadValue = roadSpend;
    fireValue = fireSpend;
    policeValue = policeSpend;
    updateFundEffects();
}


void Micropolis::doBudgetNow(bool fromMenu)
{
    // Remember the request before this function scales a broke city down
    // to the cash on hand. The sliders then show the affordable share.
    roadPercentRequested = roadPercent;
    firePercentRequested = firePercent;
    policePercentRequested = policePercent;
    roadFundingTouched = false;
    fireFundingTouched = false;
    policeFundingTouched = false;

    if (!fromMenu) {
        // Cash on hand before this year's budget is applied. The window
        // shows this as Previous funds. Current funds is totalFunds.
        budgetAnchorFunds = totalFunds;
        budgetAnchorValid = true;
    }

    Quad fireInt   = (int)(fireFund   * firePercent);
    Quad policeInt = (int)(policeFund * policePercent);
    Quad roadInt   = (int)(roadFund   * roadPercent);

    Quad total = fireInt + policeInt + roadInt;

    Quad yumDuckets = taxFund + totalFunds;

    if (yumDuckets > total) {

        // Enough yumDuckets to fully fund fire, police and road.

        fireValue = fireInt;
        policeValue = policeInt;
        roadValue = roadInt;

        /// @todo Why are we not subtracting from yumDuckets what we
        /// spend, like the code below is doing?

    } else if (total > 0) {

        assert(yumDuckets <= total);

        // Not enough yumDuckets to fund everything.
        // First spend on roads, then on fire, then on police.

        if (yumDuckets > roadInt) {

            // Enough yumDuckets to fully fund roads.

            roadValue = roadInt;
            yumDuckets -= roadInt;

            if (yumDuckets > fireInt) {

                // Enough yumDuckets to fully fund fire.

                fireValue = fireInt;
                yumDuckets -= fireInt;

                if (yumDuckets > policeInt) {

                    // Enough yumDuckets to fully fund police.
                    // Hey what are we doing here? Should never get here.
                    // We tested for yumDuckets > total above
                    // (where total = fireInt + policeInt + roadInt),
                    // so this should never happen.

                    policeValue = policeInt;
                    yumDuckets -= policeInt;

                } else {

                    // Fuly funded roads and fire.
                    // Partially fund police.

                    policeValue = yumDuckets;

                    if (yumDuckets > 0) {

                        // Scale back police percentage to available cash.

                        policePercent = ((float)yumDuckets) / ((float)policeFund);

                    } else {

                        // Exactly nothing left, so scale back police percentage to zero.

                        policePercent = 0.0;

                    }

                }

            } else {

                // Not enough yumDuckets to fully fund fire.

                fireValue = yumDuckets;

                // No police after funding roads and fire.

                policeValue = 0;
                policePercent = 0.0;

                if (yumDuckets > 0) {

                    // Scale back fire percentage to available cash.

                    firePercent =
                        ((float)yumDuckets) / ((float)fireFund);

                } else {

                  // Exactly nothing left, so scale back fire percentage to zero.

                  firePercent = 0.0;

                }

            }

        } else {

            // Not enough yumDuckets to fully fund roads.

            roadValue = yumDuckets;

            // No fire or police after funding roads.

            fireValue = 0;
            policeValue = 0;
            firePercent = 0.0;
            policePercent = 0.0;

            if (yumDuckets > 0) {

                // Scale back road percentage to available cash.

                roadPercent = ((float)yumDuckets) / ((float)roadFund);

            } else {

                // Exactly nothing left, so scale back road percentage to zero.

                roadPercent = 0.0;

            }

        }

    } else {

        assert(yumDuckets == total);
        assert(total == 0);

        // Zero funding, so no values but full percentages.

        fireValue = 0;
        policeValue = 0;
        roadValue = 0;
        firePercent = 1.0;
        policePercent = 1.0;
        roadPercent = 1.0;

    }

noMoney:

    if (!autoBudget || fromMenu) {

        // FIXME: This might have blocked on the Mac, but now it's asynchronous.
        // Make sure the stuff we do just afterwards is intended to be done immediately
        // and is not supposed to wait until after the budget dialog is dismissed.
        // Otherwise don't do it after this and arrange for it to happen when the
        // modal budget dialog is dismissed.
        if (!fromMenu) {
            // Sliders have to be able to change this year's rates before
            // the money moves. commitBudgetPayment() runs when the window
            // closes, or at the next tax collection if it is still open.
            // The flag is set before the callback so the UI can pause the
            // city on this same stack, before another tick moves cityTime.
            fireSpend = fireValue;
            policeSpend = policeValue;
            roadSpend = roadValue;
            budgetAwaitingAccept = true;
        }

        showBudgetWindowAndStartWaiting();

        mustDrawBudget = 1;
        doUpdateHeads();

    } else { /* autoBudget & !fromMenu */

        // FIXME: Not sure yumDuckets is the right value here. It gets the
        // amount spent subtracted from it above in some cases, but not if
        // we are fully funded. I think we want to use the original value
        // of yumDuckets, which is taxFund + totalFunds.

        if (yumDuckets > total) {

            Quad moreDough = (Quad)(taxFund - total);
            spend(-moreDough);

            fireSpend = fireFund;
            policeSpend = policeFund;
            roadSpend = roadFund;

            mustDrawBudget = 1;
            doUpdateHeads();

        } else {

            setAutoBudget(false); /* force autobudget */
            autoBudgetForcedOff = true;
            mustUpdateOptions = true;
            sendMessage(MESSAGE_NO_MONEY, NOWHERE, NOWHERE, false, true);
            goto noMoney;

        }

    }

}


void Micropolis::updateBudget()
{
    /// @todo The scripting language should pull these raw values out
    ///       and format them, instead of the simulator core formatting
    ///       them and pushing them out.

    if (mustDrawBudget) {
        callback("update", "s", "budget");
        mustDrawBudget = 0;
    }
}


void Micropolis::showBudgetWindowAndStartWaiting()
{
    callback("showBudgetAndWait", "");
}


void Micropolis::setCityTax(short tax)
{
    cityTax = tax;
    callback("update", "s", "taxRate");
}


////////////////////////////////////////////////////////////////////////
