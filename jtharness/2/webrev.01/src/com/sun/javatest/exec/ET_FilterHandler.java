/*
 * $Id$
 *
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

package com.sun.javatest.exec;

import com.sun.javatest.AllTestsFilter;
import com.sun.javatest.Harness;
import com.sun.javatest.InterviewParameters;
import com.sun.javatest.LastRunFilter;
import com.sun.javatest.ParameterFilter;
import com.sun.javatest.Parameters;
import com.sun.javatest.TestFilter;
import com.sun.javatest.TestResult;
import com.sun.javatest.TestSuite;
import com.sun.javatest.exec.Session.Event;
import com.sun.javatest.tool.Preferences;
import com.sun.javatest.tool.UIFactory;
import com.sun.javatest.util.PrefixMap;

import javax.swing.Action;
import javax.swing.JCheckBox;
import javax.swing.JComponent;
import javax.swing.JDialog;
import javax.swing.JMenu;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JTextArea;
import java.awt.BorderLayout;
import java.awt.EventQueue;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.Vector;

/**
 * This class handles all the special filter juggling that exec tool needs to do.
 */
public class ET_FilterHandler implements ET_FilterControl, Session.Observer {
    // preferences constants
    private static final String FILTER_PREFIX = "exec.vfilters";
    private static final String BTF_PREFIX = FILTER_PREFIX + ".btf";
    private static final String META_ID = "meta_tsid";
    private static final String META_NAME = "meta_tsn";
    private static final String META_CLASS = "meta_class";
    protected Vector<TestFilter> allFilters;
    private FilterConfig fConfig;
    private FilterSelectionHandler fHandler;
    private ExecModel model;
    private UIFactory uif;
    private JComponent parentComponent;
    private Map<String, String> map;        // saved desktop map to restore from
    // filters
    private LastRunFilter ltrFilter;        // last test run
    private ParameterFilter paramFilter;    // current param filter
    private BasicCustomTestFilter bctf;     // "custom" filter
    private AllTestsFilter allFilter;
    private Collection<TestFilter> certFilters;          // "certification" filters
    // custom filter info
    private TestSuite lastTs;

    ET_FilterHandler(JComponent parent, ExecModel model, Harness h, UIFactory uif,
                     Map<String, String> map) {
        this(parent, model, uif);
        setHarness(h);
        restore(map);
    }

    protected ET_FilterHandler(JComponent parent, ExecModel model, UIFactory uif) {
        this.uif = uif;
        this.model = model;
        this.parentComponent = parent;

        allFilters = new Vector<>();

    }

    @Override
    public void setHarness(Harness h) {
        h.addObserver(new Watcher());
    }

    FilterConfig loadFilters() {
        // this method may eventually do fancy things to scan the classpath or
        // preferences for custom plugin tools, for now it is hardcoded

        if (fConfig != null) {
            return fConfig;
        }

        fConfig = new FilterConfig(model, parentComponent, uif);

        fHandler = fConfig.createFilterSelectionHandler();

        // add observer here so that the menu gets the additions
        // also watches for user selection of new filter
        /*
        filterWatcher = new FilterWatcher();
        filterHandler.addObserver(filterWatcher);
        */

        // last run filter
        TestFilter filter = ltrFilter = new LastRunFilter();
        allFilters.add(filter);
        fConfig.add(filter);

        // current parameter filter
        filter = paramFilter = new ParameterFilter();
        allFilters.add(filter);
        fConfig.add(filter);

        List<TestFilter> usersFilters = getUsersFilters();
        if (usersFilters != null) {
            for (TestFilter tf : usersFilters) {
                allFilters.add(tf);
                fConfig.add(tf);
            }
        }
/*
        if (model.getContextManager() != null &&
                model.getContextManager().getFeatureManager() != null) {
            if (model.getContextManager().getFeatureManager().isEnabled(FeatureManager.TEMPLATE_USAGE)) {
                tfilter = new TemplateParameterFilter();
                allFilters.add(tfilter);
                fConfig.add(tfilter);
            }
        }
 */

        // filter which accepts all tests
        allFilter = new AllTestsFilter();
        allFilters.add(allFilter);
        fConfig.add(allFilter);

        updateCustomFilter();

        // establish the default
        fHandler.setFilter(getDefaultFilter(map));

        return fConfig;
    }

    /**
     * Subclasses may override this method to insert filters
     * like TemplateFilter
     *
     * @return list of filters defined for the User's TestSuite, or null
     */
    protected List<TestFilter> getUsersFilters() {
        return null;
    }

    @Override
    public JMenu getFilterMenu() {
        return getFilterSelectionHandler().getFilterMenu();
    }

    FilterSelectionHandler getFilterSelectionHandler() {
        loadFilters();
        return fHandler;
    }

    private TestFilter getDefaultFilter(Map<String, String> map) {
        if (map != null) {
            String pref = map.get(ExecTool.ACTIVE_FILTER);

            // try to use filter indicated in preference
            for (TestFilter filter : allFilters) {
                if (filter.getClass().getName().equals(pref)) {
                    return filter;
                }
            }   // for
        }

        // default filter
        return allFilter;
    }

    protected void updateFilters() {
        loadFilters();

        // special code for custom filter loading
        updateCustomFilter();

        // update Last Test Run filtered if needed
        if (!ltrFilter.isWorkDirectorySet()) {
            ltrFilter.setWorkDirectory(model.getWorkDirectory());
        }

        InterviewParameters ips = model.getInterviewParameters();
        if (ips == null)        // this filter does not apply now
        {
            return;
        }

        paramFilter.update(ips);

        List<TestFilter> newCertFilters = ips.getAllRelevantFiltersInTheSuite();

        // check for filter behavior equality
        if (newCertFilters.isEmpty()) {
            if (certFilters != null) {
                // we had a certification filter earlier, now it is gone
                if (certFilters.contains(fHandler.getActiveFilter())) {
                    // switch to another filter before removing
                    // XXX may want to notify user!
                    fHandler.setFilter(paramFilter);
                }

                certFilters.forEach(fConfig::remove);
            } else {
                // FilterConfig is clean
            }
        }   // outer if
        else if (!newCertFilters.equals(certFilters)) {
            // check for reference equality
            if (newCertFilters == certFilters) {
                // this is ignored by fConfig if it is not relevant
                certFilters.forEach(fConfig::notifyUpdated);
            } else {
                // rm old one, put in new one
                newCertFilters.forEach(fConfig::add);

                if (certFilters.contains(fHandler.getActiveFilter())) {
                    // switch to another filter before removing
                    // XXX may want to notify user!
                    if ( !newCertFilters.isEmpty()) {
                        fHandler.setFilter(newCertFilters.get(0));
                    }
                }

                certFilters.forEach(fConfig::remove);
                certFilters = newCertFilters;
            }
        } else {
            // filter is the same
        }
    }

    @Override
    public JMenu getMenu() {
        loadFilters();
        return null;
        //return fHandler.getFilterMenu();
    }

    public FilterConfig getFilterConfig() {
        return fConfig;
    }

    /**
     * Save internal state.
     */
    @Override
    public void save(Map<String, String> m) {
        // -------- saved to given map (desktop) -------
        Preferences prefs = Preferences.access();
        TestFilter aFilter = fHandler.getActiveFilter();
        m.put(ExecTool.ACTIVE_FILTER, aFilter.getClass().getName());

        // -------- saved to global prefs -------
        TestSuite ts = model.getTestSuite();
        String tsId = null;
        String tsName = null;
        if (ts != null) {
            tsId = ts.getID();
            tsName = ts.getName();
        }

        int prefIndex = getPreferenceIndexForWrite(prefs, tsId);

        ConstrainedPreferenceMap cpm = new ConstrainedPreferenceMap(prefs);
        // using meta_ prefix for info not written by the filter itself
        Map<String, String> pm = new PrefixMap<>(cpm, FILTER_PREFIX + prefIndex);

        // it's really a special case to have a pref. entry which does not
        // have a tsId associated
        // it should really only be used (if at all) if a default set of
        // settings is being saved
        if (tsId != null) {
            pm.put(META_ID, tsId);
            pm.put(META_NAME, tsName);
        }

        pm.put(META_CLASS, bctf.getClass().getName());
        bctf.save(pm);

        prefs.save();
    }

    @Override
    public void restore(Map<String, String> m) {
        this.map = m;
        fHandler.setFilter(getDefaultFilter(m));
    }

    @Override
    public void updateGUI() {
        // do nothing
    }

    @Override
    public List<Action> getToolBarActionList() {
        return null;
    }

    @Override
    public void dispose() {
        // do nothing
    }

    private void updateCustomFilter() {
        // we only go thru here once per init.
        if (lastTs != null) {
            return;
        }

        // load from prefs
        lastTs = model.getTestSuite();
        String tsId = null;
        String tsName = null;

        // may be null, meaning that the exec tool has no TS
        if (lastTs != null) {
            tsId = lastTs.getID();
            tsName = lastTs.getName();
        }

        Preferences prefs = Preferences.access();
        int prefIndex = getPreferenceIndexForRead(prefs, tsId);

        // using META_ prefix for info not written by the filter itself
        // XXX could check value of c in the future
        //String c = prefs.getPreference(FILTER_PREFIX + "." + prefIndex + META_CLASS);

        if (prefIndex >= 0) {
            // load previous settings
            ConstrainedPreferenceMap cpm = new ConstrainedPreferenceMap(prefs);
            PrefixMap<String> pm = new PrefixMap<>(cpm, FILTER_PREFIX + prefIndex);

            if (bctf == null) {     // init
                bctf = new BasicCustomTestFilter(pm, model, uif);
                allFilters.add(bctf);
                fConfig.add(bctf);
            } else {                  // tell filter load settings
                bctf.load(pm);
                fHandler.updateFilterMetaInfo(bctf);
            }
        } else if (bctf == null) {
            // no previous settings to use
            bctf = new BasicCustomTestFilter(model, uif);
            allFilters.add(bctf);
            fConfig.add(bctf);
        }

    }

    /**
     * Find the index in the preferences which is appropriate for this filter
     * to save its info in.  Returns the next available one if there isn't
     * an existing one.
     *
     * @param tsId May be null.
     */
    private int getPreferenceIndexForWrite(Preferences p, String tsId) {
        // pref. index 0 is the default when no tsId is available
        // pref. encoding is:
        // FILTER_PREFIX + <number> + <filter data>
        int index = 0;
        int numFilters = getPreferenceCount(p);

        if (tsId != null && numFilters != 0) {
            index = getPreferenceIndex(p, tsId, numFilters);

            if (index == -1) {      // not found
                index = ++numFilters;
                p.setPreference(FILTER_PREFIX + ".count",
                        Integer.toString(numFilters));
            }
        } else if (tsId != null && numFilters == 0) {
            index = 1;
            numFilters = 1;
            p.setPreference(FILTER_PREFIX + ".count",
                    Integer.toString(numFilters));
        } else {
            // index remains 0, the default
        }
        return index;
    }

    /**
     * Which pref index should be read for the given test suite.
     *
     * @return -1 if there is no pref. to read.
     */
    private int getPreferenceIndexForRead(Preferences p, String tsId) {
        int numFilters = getPreferenceCount(p);
        int result = -1;

        if (numFilters == 0) {
            result = -1;
        } else {
            result = getPreferenceIndex(p, tsId, numFilters);

            // read default values from index 0 if a match for the given
            // TS was not found
            /*
            if (result == -1 && numFilters > 0)
                result = 0;
            */
        }

        return result;
    }

    /**
     * Do not call this directly.
     *
     * @param numFilters A number greater than zero.
     * @return -1 if not found.
     */
    private int getPreferenceIndex(Preferences p, String tsId, int numFilters) {
        int index = -1;

        for (int i = 1; i <= numFilters; i++) {
            String id = p.getPreference(FILTER_PREFIX + i + "." + META_ID);
            if (id.equals(tsId)) {
                index = i;
                break;
            }
        }   // for

        if (index > numFilters) {
            return -1;
        } else {
            return index;
        }
    }

    /**
     * How many indexes are we using for filters right now.
     *
     * @return -1 for none.
     */
    private int getPreferenceCount(Preferences p) {

        return Integer.parseInt(
                p.getPreference(FILTER_PREFIX + ".count", "0"));
    }

    @Override
    public void updated(Event ev) {
        if (ev instanceof BasicSession.E_NewConfig) {
            paramFilter.update(((BasicSession.E_NewConfig) ev).ip);
        }
        updateFilters();
    }

    /**
     * This class is completely private and only implements what we
     * want to use here.
     */
    private static class ConstrainedPreferenceMap implements Map<String, String> {
        private Preferences prefs;

        ConstrainedPreferenceMap(Preferences p) {
            prefs = p;
        }

        @Override
        public void clear() {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean containsKey(Object o) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean containsValue(Object v) {
            throw new UnsupportedOperationException();
        }

        @Override
        public Set<Map.Entry<String, String>> entrySet() {
            throw new UnsupportedOperationException();
        }

        @Override
        public String get(Object key) {
            if (!(key instanceof String)) {
                throw new IllegalArgumentException("key must be a string");
            }

            return prefs.getPreference((String) key);
        }

        @Override
        public boolean isEmpty() {
            throw new UnsupportedOperationException();
        }

        @Override
        public Set<String> keySet() {
            throw new UnsupportedOperationException();
        }

        @Override
        public String put(String key, String value) {
            prefs.setPreference(key, value);
            return null;
        }

        @Override
        public void putAll(Map<? extends String, ? extends String> t) {
            throw new UnsupportedOperationException();
        }

        @Override
        public String remove(Object key) {
            throw new UnsupportedOperationException();
        }

        @Override
        public int size() {
            throw new UnsupportedOperationException();
        }

        @Override
        public Collection<String> values() {
            throw new UnsupportedOperationException();
        }

        public String get(String key) {
            return prefs.getPreference(key);
        }
    }

    private static class FilterWatcher implements FilterSelectionHandler.Observer {
        // NOTE: disconnected in loadFilters()
        // ---------- FilterConfig.Observer ----------
        @Override
        public void filterUpdated(TestFilter f) {
            // ignore here
        }

        @Override
        public void filterSelected(TestFilter f) {
            // change menu selection
            /* XXX not implemented yet
            int index = items.getValueIndex(f);

            if (index != -1) {
                filterMenu.setSelected((Component)(items.getKeyAt(index)));
            }
            */

            // XXX avoid poking an uninitialized GUI what is a better check
            //if (testTreePanel != null)
            //      updateGUI();
        }

        @Override
        public void filterAdded(TestFilter f) {
            // add to menu
            /* XXX not implemented yet
            JMenuItem mi = new JRadioButtonMenuItem(f.getName());
            mi.addActionListener(this);
            filterMenu.add(mi);
            items.put(mi, f);
            */
        }

        @Override
        public void filterRemoved(TestFilter f) {
            // rm from menu
            /* XXX not implemented yet
            int index = items.getValueIndex(f);
            filterMenu.remove(index);
            items.remove(index);
            */
        }
    }

    class Watcher implements Harness.Observer {
        @Override
        public void startingTestRun(Parameters params) {
            ltrFilter.setLastStartTime(System.currentTimeMillis());
            ltrFilter.clearTestURLs();

            if (fHandler.getActiveFilter() == allFilter) {
                final Preferences p = Preferences.access();
                if (p.getPreference(ExecTool.FILTER_WARN_PREF, "true").equals("true")) {
                    final JPanel pan = uif.createPanel("notagain", false);
                    final JCheckBox cb = uif.createCheckBox("exec.fltr.noShow",
                            false);
                    final JTextArea msg = uif.createMessageArea("exec.fltr.note");
                    EventQueue.invokeLater(() -> {
                        pan.setLayout(new BorderLayout());
                        pan.add(cb, BorderLayout.SOUTH);
                        pan.add(msg, BorderLayout.CENTER);

                        JOptionPane pane = new JOptionPane(pan, JOptionPane.INFORMATION_MESSAGE,
                                JOptionPane.DEFAULT_OPTION, null, null, null);
                        JDialog dialog = pane.createDialog(parentComponent, uif.getI18NString("exec.fltr.note.title"));
                        dialog.show();

                        // can't use this, it doesn't indicate if the user pressed
                        // OK or canceled the dialog some other way
                        //uif.showCustomOptionDialog("exec.fltr.note", pan);

                        Object selectedValue = pane.getValue();
                        if ((selectedValue instanceof Integer) &&
                                ((Integer) selectedValue).intValue() >= 0) {
                            p.setPreference(ExecTool.FILTER_WARN_PREF,
                                    Boolean.toString(!cb.isSelected()));
                        }
                    });
                }
            }   // if
        }

        @Override
        public void startingTest(TestResult tr) {
            ltrFilter.addTestURL(tr.getTestName());
        }

    }
}

