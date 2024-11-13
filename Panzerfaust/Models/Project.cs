using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Models
{
    public class Project
    {
        public string Name { get; set; } = string.Empty;
        public string Fullpath { get; set; } = string.Empty;
        public DateTime CreationDate { get; set; }
        public DateTime UpdateDate { get; set; }


        public string GetHash()
        {
            using (SHA256 sha256 = SHA256.Create())
            {
                // Convert the input string to a byte array and compute the hash
                byte[] bytes = sha256.ComputeHash(Encoding.UTF8.GetBytes(Name));

                // Convert the byte array to a hexadecimal string
                StringBuilder sb = new StringBuilder();
                foreach (byte b in bytes)
                {
                    sb.Append(b.ToString("x2")); // Format byte as hex
                }

                return sb.ToString();
            }
        }
    }
}
