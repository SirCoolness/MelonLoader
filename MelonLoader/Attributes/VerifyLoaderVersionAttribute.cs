﻿using System;
using Semver;

namespace MelonLoader
{
    [AttributeUsage(AttributeTargets.Assembly)]
    public class VerifyLoaderVersionAttribute : Attribute
    {
        /// <summary>
        /// Specified SemVersion.
        /// </summary>
        public SemVersion SemVer { get; internal set; }

        /// <summary>
        /// Specified Version Major.
        /// </summary>
        public int Major { get; }

        /// <summary>
        /// Specified Version Minor.
        /// </summary>
        public int Minor { get; }

        /// <summary>
        /// Specified Version Patch.
        /// </summary>
        public int Patch { get; }

        /// <summary>
        /// Specified Version Revision.
        /// </summary>
        public int Revision { get; }

        /// <summary>
        /// If Version Specified is a Minimum.
        /// </summary>
        public bool IsMinimum { get; internal set; }


        public VerifyLoaderVersionAttribute(int major, int minor, int patch) : this(new SemVersion(major, minor, patch), false) { }
        public VerifyLoaderVersionAttribute(int major, int minor, int patch, bool is_minimum) : this(new SemVersion(major, minor, patch), is_minimum) { }
        public VerifyLoaderVersionAttribute(string version) : this(version, false) { }
        public VerifyLoaderVersionAttribute(string version, bool is_minimum) : this(SemVersion.Parse(version), is_minimum) { }
        public VerifyLoaderVersionAttribute(SemVersion semver, bool is_minimum)
        {
            SemVer = semver;
            IsMinimum = is_minimum;
        }
    }
}