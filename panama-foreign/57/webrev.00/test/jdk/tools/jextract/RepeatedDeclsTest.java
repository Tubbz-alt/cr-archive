/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
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

import org.testng.annotations.Test;

import java.lang.reflect.Method;
import java.nio.file.Path;
import jdk.incubator.foreign.GroupLayout;
import jdk.incubator.foreign.MemoryAddress;
import jdk.incubator.foreign.MemoryLayout;
import jdk.incubator.foreign.MemorySegment;
import jdk.incubator.foreign.SystemABI.Type;
import static org.testng.Assert.assertEquals;
import static org.testng.Assert.assertNotNull;
import static org.testng.Assert.assertTrue;

/*
 * @test
 * @bug 8240300
 * @summary jextract produces non compilable code with repeated declarations
 * @modules jdk.incubator.jextract
 * @build JextractToolRunner
 * @run testng/othervm -Djdk.incubator.foreign.Foreign=permit RepeatedDeclsTest
 */
public class RepeatedDeclsTest extends JextractToolRunner {
    @Test
    public void repeatedDecls() {
        Path repeatedDeclsOutput = getOutputFilePath("repeatedDeclsgen");
        Path repeatedDeclsH = getInputFilePath("repeatedDecls.h");
        run("-d", repeatedDeclsOutput.toString(), repeatedDeclsH.toString()).checkSuccess();
        try(Loader loader = classLoader(repeatedDeclsOutput)) {
            Class<?> cls = loader.loadClass("repeatedDecls_h");
            // check a method for "void func(int)"
            assertNotNull(findMethod(cls, "func", int.class));

            // check a method for "void func2(int)"
            assertNotNull(findMethod(cls, "func2", int.class));

            // check a method for "void func3(int*)"
            assertNotNull(findMethod(cls, "func3", MemoryAddress.class));

            // check a method for "void func4(int*)"
            assertNotNull(findMethod(cls, "func4", MemoryAddress.class));

            // check a method for "void func5(int)"
            assertNotNull(findMethod(cls, "func5", int.class));

            // check a method for "double distance(struct Point)"
            assertNotNull(findMethod(cls, "distance", MemorySegment.class));

            // check a getter method for "i"
            assertNotNull(findMethod(cls, "i$get"));

            // check a setter method for "i"
            assertNotNull(findMethod(cls, "i$set", int.class));

            // make sure that enum constants are generated fine
            checkIntField(cls, "R", 0);
            checkIntField(cls, "G", 1);
            checkIntField(cls, "B", 2);
            checkIntField(cls, "C", 0);
            checkIntField(cls, "M", 1);
            checkIntField(cls, "Y", 2);

            // check Point layout
            MemoryLayout pointLayout = findLayout(cls, "Point");
            assertNotNull(pointLayout);
            assertTrue(((GroupLayout)pointLayout).isStruct());
            checkFieldABIType(pointLayout, "i",  Type.INT);
            checkFieldABIType(pointLayout, "j",  Type.INT);

            // check Point3D layout
            MemoryLayout point3DLayout = findLayout(cls, "Point3D");
            assertNotNull(point3DLayout);
            assertTrue(((GroupLayout)point3DLayout).isStruct());
            checkFieldABIType(point3DLayout, "i",  Type.INT);
            checkFieldABIType(point3DLayout, "j",  Type.INT);
            checkFieldABIType(point3DLayout, "k",  Type.INT);
        } finally {
            deleteDir(repeatedDeclsOutput);
        }
    }
}
