/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package org.openjdk.skara.bots.bridgekeeper;

import org.openjdk.skara.bot.*;
import org.openjdk.skara.forge.*;

import java.nio.file.Path;
import java.time.*;
import java.util.*;
import java.util.logging.Logger;

class PullRequestPrunerBotWorkItem implements WorkItem {
    private final Logger log = Logger.getLogger("org.openjdk.skara.bots");;
    private final HostedRepository repository;
    private final PullRequest pr;
    private final Duration maxAge;

    PullRequestPrunerBotWorkItem(HostedRepository repository, PullRequest pr, Duration maxAge) {
        this.pr = pr;
        this.repository = repository;
        this.maxAge = maxAge;
    }

    @Override
    public boolean concurrentWith(WorkItem other) {
        if (!(other instanceof PullRequestPrunerBotWorkItem)) {
            return true;
        }
        PullRequestPrunerBotWorkItem otherItem = (PullRequestPrunerBotWorkItem) other;
        if (!pr.id().equals(otherItem.pr.id())) {
            return true;
        }
        if (!repository.name().equals(otherItem.repository.name())) {
            return true;
        }
        return false;
    }

    // Prune durations are on the order of days and weeks
    private String formatDuration(Duration duration) {
        var count = duration.toDays();
        var unit = "day";

        if (count > 14) {
            count /= 7;
            unit = "week";
        }
        if (count != 1) {
            unit += "s";
        }
        return count + " " + unit;
    }

    @Override
    public void run(Path scratchPath) {
        var message = "@" + pr.author().userName() + " This pull request has been inactive for more than " +
                formatDuration(maxAge) + " and will be automatically closed. If you think this is incorrect, " +
                "feel free to reopen it!";

        log.fine("Posting prune message");
        pr.addComment(message);

        pr.setState(PullRequest.State.CLOSED);
    }
}

public class PullRequestPrunerBot implements Bot {
    private final HostedRepository repository;
    private final Duration maxAge;

    PullRequestPrunerBot(HostedRepository repository, Duration maxAge) {
        this.repository = repository;
        this.maxAge = maxAge;
    }

    @Override
    public List<WorkItem> getPeriodicItems() {
        List<WorkItem> ret = new LinkedList<>();
        var oldestAllowed = ZonedDateTime.now().minus(maxAge);

        for (var pr : repository.pullRequests()) {
            if (pr.updatedAt().isBefore(oldestAllowed)) {
                var item = new PullRequestPrunerBotWorkItem(repository, pr, maxAge);
                ret.add(item);
            }
        }

        return ret;
    }
}
