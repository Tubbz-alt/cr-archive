/*
 * Copyright (c) 2001, 2020, Oracle and/or its affiliates. All rights reserved.
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

package jdk.javadoc.internal.doclets.toolkit.taglets;

import java.util.Collections;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;

import javax.lang.model.element.Element;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.type.ExecutableType;
import javax.lang.model.type.TypeMirror;

import com.sun.source.doctree.DocTree;

import jdk.javadoc.doclet.Taglet.Location;
import jdk.javadoc.internal.doclets.toolkit.Content;
import jdk.javadoc.internal.doclets.toolkit.util.CommentHelper;
import jdk.javadoc.internal.doclets.toolkit.util.DocFinder;
import jdk.javadoc.internal.doclets.toolkit.util.DocFinder.Input;
import jdk.javadoc.internal.doclets.toolkit.util.Utils;

/**
 * A taglet that represents the @throws tag.
 *
 *  <p><b>This is NOT part of any supported API.
 *  If you write code that depends on this, you do so at your own risk.
 *  This code and its internal interfaces are subject to change or
 *  deletion without notice.</b>
 */
public class ThrowsTaglet extends BaseTaglet
    implements InheritableTaglet {

    public ThrowsTaglet() {
        super(DocTree.Kind.THROWS, false, EnumSet.of(Location.CONSTRUCTOR, Location.METHOD));
    }

    @Override
    public void inherit(DocFinder.Input input, DocFinder.Output output) {
        Utils utils = input.utils;
        Element exception;
        CommentHelper ch = utils.getCommentHelper(input.element);
        if (input.tagId == null) {
            exception = ch.getException(utils.configuration, input.docTreeInfo.docTree);
            input.tagId = exception == null
                    ? ch.getExceptionName(input.docTreeInfo.docTree).getSignature()
                    : utils.getFullyQualifiedName(exception);
        } else {
            TypeElement element = input.utils.findClass(input.element, input.tagId);
            exception = (element == null) ? null : element;
        }

        for (DocTree dt : input.utils.getThrowsTrees(input.element)) {
            Element texception = ch.getException(utils.configuration, dt);
            if (texception != null && (input.tagId.equals(utils.getSimpleName(texception)) ||
                 (input.tagId.equals(utils.getFullyQualifiedName(texception))))) {
                output.holder = input.element;
                output.holderTag = dt;
                output.inlineTags = ch.getBody(input.utils.configuration, output.holderTag);
                output.tagList.add(dt);
            } else if (exception != null && texception != null &&
                    utils.isTypeElement(texception) && utils.isTypeElement(exception) &&
                    utils.isSubclassOf((TypeElement)texception, (TypeElement)exception)) {
                output.tagList.add(dt);
            }
        }
    }

    /**
     * Add links for exceptions that are declared but not documented.
     */
    private Content linkToUndocumentedDeclaredExceptions(List<? extends TypeMirror> declaredExceptionTypes,
            Set<String> alreadyDocumented, TagletWriter writer) {
        Utils utils = writer.configuration().utils;
        Content result = writer.getOutputInstance();
        //Add links to the exceptions declared but not documented.
        for (TypeMirror declaredExceptionType : declaredExceptionTypes) {
            TypeElement klass = utils.asTypeElement(declaredExceptionType);
            if (klass != null &&
                !alreadyDocumented.contains(declaredExceptionType.toString()) &&
                !alreadyDocumented.contains(utils.getFullyQualifiedName(klass, false))) {
                if (alreadyDocumented.isEmpty()) {
                    result.add(writer.getThrowsHeader());
                }
                result.add(writer.throwsTagOutput(declaredExceptionType));
                alreadyDocumented.add(utils.getSimpleName(klass));
            }
        }
        return result;
    }

    /**
     * Inherit throws documentation for exceptions that were declared but not
     * documented.
     */
    private Content inheritThrowsDocumentation(Element holder,
            List<? extends TypeMirror> declaredExceptionTypes, Set<String> alreadyDocumented,
            Map<String, TypeMirror> typeSubstitutions, TagletWriter writer) {
        Utils utils = writer.configuration().utils;
        Content result = writer.getOutputInstance();
        if (utils.isExecutableElement(holder)) {
            Map<List<? extends DocTree>, ExecutableElement> declaredExceptionTags = new LinkedHashMap<>();
            for (TypeMirror declaredExceptionType : declaredExceptionTypes) {
                Input input = new DocFinder.Input(utils, holder, this,
                        utils.getTypeName(declaredExceptionType, false));
                DocFinder.Output inheritedDoc = DocFinder.search(writer.configuration(), input);
                if (inheritedDoc.tagList.isEmpty()) {
                    String typeName = utils.getTypeName(declaredExceptionType, true);
                    input = new DocFinder.Input(utils, holder, this, typeName);
                    inheritedDoc = DocFinder.search(writer.configuration(), input);
                }
                if (!inheritedDoc.tagList.isEmpty()) {
                    if (inheritedDoc.holder == null) {
                        inheritedDoc.holder = holder;
                    }
                    declaredExceptionTags.put(inheritedDoc.tagList, (ExecutableElement)inheritedDoc.holder);
                }
            }
            result.add(throwsTagsOutput(declaredExceptionTags, writer, alreadyDocumented,
                    typeSubstitutions, false));
        }
        return result;
    }

    @Override
    public Content getTagletOutput(Element holder, TagletWriter writer) {
        Utils utils = writer.configuration().utils;
        ExecutableElement execHolder = (ExecutableElement) holder;
        ExecutableType instantiatedType = utils.asInstantiatedMethodType(
                writer.getCurrentPageElement(), (ExecutableElement)holder);
        List<? extends TypeMirror> thrownTypes = instantiatedType.getThrownTypes();
        Map<String, TypeMirror> typeSubstitutions = getSubstitutedThrownTypes(
                ((ExecutableElement) holder).getThrownTypes(), thrownTypes);
        Map<List<? extends DocTree>, ExecutableElement> tagsMap = new LinkedHashMap<>();
        tagsMap.put(utils.getThrowsTrees(execHolder), execHolder);
        Content result = writer.getOutputInstance();
        HashSet<String> alreadyDocumented = new HashSet<>();
        if (!tagsMap.isEmpty()) {
            result.add(throwsTagsOutput(tagsMap, writer, alreadyDocumented, typeSubstitutions, true));
        }
        result.add(inheritThrowsDocumentation(holder,
                thrownTypes, alreadyDocumented, typeSubstitutions, writer));
        result.add(linkToUndocumentedDeclaredExceptions(thrownTypes, alreadyDocumented, writer));
        return result;
    }

    /**
     * Given an array of <code>Tag</code>s representing this custom
     * tag, return its string representation.
     * @param throwTags the array of <code>ThrowsTag</code>s to convert.
     * @param writer the TagletWriter that will write this tag.
     * @param alreadyDocumented the set of exceptions that have already
     *        been documented.
     * @param allowDups True if we allow duplicate throws tags to be documented.
     * @return the Content representation of this <code>Tag</code>.
     */
    protected Content throwsTagsOutput(Map<List<? extends DocTree>, ExecutableElement> throwTags,
                                       TagletWriter writer, Set<String> alreadyDocumented,
                                       Map<String,TypeMirror> typeSubstitutions, boolean allowDups) {
        Utils utils = writer.configuration().utils;
        Content result = writer.getOutputInstance();
        if (!throwTags.isEmpty()) {
            for (Entry<List<? extends DocTree>, ExecutableElement> entry : throwTags.entrySet()) {
                CommentHelper ch = utils.getCommentHelper(entry.getValue());
                Element e = entry.getValue();
                for (DocTree dt : entry.getKey()) {
                    Element te = ch.getException(utils.configuration, dt);
                    String excName = ch.getExceptionName(dt).toString();
                    TypeMirror substituteType = typeSubstitutions.get(excName);
                    if ((!allowDups) &&
                        (alreadyDocumented.contains(excName) ||
                        (te != null && alreadyDocumented.contains(utils.getFullyQualifiedName(te, false)))) ||
                        (substituteType != null && alreadyDocumented.contains(substituteType.toString()))) {
                        continue;
                    }
                    if (alreadyDocumented.isEmpty()) {
                        result.add(writer.getThrowsHeader());
                    }
                    result.add(writer.throwsTagOutput(e, dt, substituteType));
                    if (substituteType != null) {
                        alreadyDocumented.add(substituteType.toString());
                    } else {
                        alreadyDocumented.add(te != null
                                ? utils.getFullyQualifiedName(te, false)
                                : excName);
                    }
                }
            }
        }
        return result;
    }

    /**
     * Returns a map of substitutions for a list of thrown types with the original type-variable
     * name as key and the instantiated type as value. If no types need to be substituted
     * an empty map is returned.
     * @param declaredThrownTypes the originally declared thrown types.
     * @param instantiatedThrownTypes the thrown types in the context of the current type.
     * @return map of declared to instantiated thrown types or an empty map.
     */
    private Map<String, TypeMirror> getSubstitutedThrownTypes(List<? extends TypeMirror> declaredThrownTypes,
                                                              List<? extends TypeMirror> instantiatedThrownTypes) {
        if (!instantiatedThrownTypes.equals(declaredThrownTypes)) {
            Map<String, TypeMirror> map = new HashMap<>();
            Iterator<? extends TypeMirror> i1 = instantiatedThrownTypes.iterator();
            Iterator<? extends TypeMirror> i2 = declaredThrownTypes.iterator();
            while (i1.hasNext() && i2.hasNext()) {
                TypeMirror t1 = i1.next();
                TypeMirror t2 = i2.next();
                if (!t1.equals(t2))
                    map.put(t2.toString(), t1);
            }
            return map;
        }
        return Collections.emptyMap();
    }
}
