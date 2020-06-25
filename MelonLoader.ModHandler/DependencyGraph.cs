﻿using System;
using System.Collections.Generic;
using System.Text;
using System.Reflection;
using System.IO;

namespace MelonLoader {
	internal class DependencyGraph<T> where T : MelonBase {

		public static void TopologicalSort(IList<T> modsOrPlugins, Func<T, string> modNameGetter) {
			DependencyGraph<T> dependencyGraph = new DependencyGraph<T>(modsOrPlugins, modNameGetter);
			modsOrPlugins.Clear();
			dependencyGraph.TopologicalSortInto(modsOrPlugins);
		}

		private readonly Vertex[] vertices;

		private DependencyGraph(IList<T> mods, Func<T, string> modNameGetter) {
			int size = mods.Count;
			vertices = new Vertex[size];
			IDictionary<string, Vertex> nameLookup = new Dictionary<string, Vertex>(size);

			// Create a vertex in the dependency graph for each mod to load
			for (int i = 0; i < size; ++i) {
				Assembly modAssembly = mods[i].Assembly;
				string modName = modNameGetter(mods[i]);

				Vertex modVertex = new Vertex(i, mods[i], modName);
				vertices[i] = modVertex;
				nameLookup[modAssembly.GetName().Name] = modVertex;
			}

			// Add an edge for each dependency between mods
			IDictionary<string, IList<AssemblyName>> modsWithMissingDeps = new SortedDictionary<string, IList<AssemblyName>>();
			List<AssemblyName> missingDependencies = new List<AssemblyName>();
			foreach (Vertex modVertex in vertices) {
				Assembly modAssembly = modVertex.mod.Assembly;
				missingDependencies.Clear();

				foreach (AssemblyName dependency in modAssembly.GetReferencedAssemblies()) {
					if (nameLookup.TryGetValue(dependency.Name, out Vertex dependencyVertex)) {
						modVertex.dependencies.Add(dependencyVertex);
						dependencyVertex.dependents.Add(modVertex);
					} else if (!TryLoad(dependency)) {
						missingDependencies.Add(dependency);
					}
				}

				if (missingDependencies.Count > 0) {
					modVertex.skipLoading = true;
					modsWithMissingDeps.Add(modNameGetter(modVertex.mod), missingDependencies.ToArray());
				}
			}

			if (modsWithMissingDeps.Count > 0) {
				// Some mods are missing dependencies. Don't load these mods and show an error message
				MelonModLogger.LogError(BuildMissingDependencyMessage(modsWithMissingDeps));
			}
		}

		// Returns true if 'assembly' was already loaded or could be loaded, false if the required assembly was missing.
		private static bool TryLoad(AssemblyName assembly) {
			try {
				Assembly.Load(assembly);
				return true;
			} catch (FileNotFoundException) {
				return false;
			} catch (Exception ex) {
				MelonModLogger.LogError("Loading mod dependency failed: " + ex);
				return false;
			}
		}

		private static string BuildMissingDependencyMessage(IDictionary<string, IList<AssemblyName>> modsWithMissingDeps) {
			StringBuilder messageBuilder = new StringBuilder("Some mods could not be loaded due to missing dependencies, which you need to install:\n");
			foreach (string modName in modsWithMissingDeps.Keys) {
				messageBuilder.Append($"- '{modName}' is missing the following dependencies:\n");
				foreach (AssemblyName dependency in modsWithMissingDeps[modName]) {
					messageBuilder.Append($"    - '{dependency.Name}' v{dependency.Version}\n");
				}
			}
			messageBuilder.Length -= 1; // Remove trailing newline
			return messageBuilder.ToString();
		}

		private void TopologicalSortInto(IList<T> loadedMods) {
			int[] unloadedDependencies = new int[vertices.Length];
			SortedList<string, Vertex> loadableMods = new SortedList<string, Vertex>();
			int skippedMods = 0;

			// Find all sinks in the dependency graph, i.e. mods without any dependencies on other mods
			for (int i = 0; i < vertices.Length; ++i) {
				Vertex vertex = vertices[i];
				int dependencyCount = vertex.dependencies.Count;

				unloadedDependencies[i] = dependencyCount;
				if (dependencyCount == 0) {
					if (!vertex.skipLoading) {
						loadableMods.Add(vertex.name, vertex);
					} else {
						++skippedMods;
					}
				}
			}

			// Perform the (reverse) topological sorting
			while (loadableMods.Count > 0) {
				Vertex mod = loadableMods.Values[0];
				loadableMods.RemoveAt(0);
				loadedMods.Add(mod.mod);

				foreach (Vertex dependent in mod.dependents) {
					unloadedDependencies[dependent.index] -= 1;
					if (unloadedDependencies[dependent.index] == 0) {
						if (!dependent.skipLoading) {
							loadableMods.Add(dependent.name, dependent);
						} else {
							++skippedMods;
						}
					}
				}
			}

			if (loadedMods.Count + skippedMods < vertices.Length) {
				StringBuilder errorMessage = new StringBuilder("Some mods could not be loaded due to a cyclic dependency:\n");
				for (int i = 0; i < vertices.Length; ++i) {
					if (unloadedDependencies[i] > 0)
						errorMessage.Append($"- '{vertices[i].name}'\n");
				}
				errorMessage.Length -= 1; // Remove trailing newline
				MelonModLogger.LogError(errorMessage.ToString());
			}
		}

		private class Vertex {

			internal readonly int index;
			internal readonly T mod;
			internal readonly string name;

			internal readonly IList<Vertex> dependencies;
			internal readonly IList<Vertex> dependents;
			internal bool skipLoading;

			internal Vertex(int index, T mod, string name) {
				this.index = index;
				this.mod = mod;
				this.name = name;

				dependencies = new List<Vertex>();
				dependents = new List<Vertex>();
				skipLoading = false;
			}
		}
	}
}
